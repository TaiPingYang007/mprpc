#include "./include/mprpcchannel.h"
#include "./include/mprpcapplication.h"
#include "./include/logger.h"
#include "rpcherder.pb.h"
#include "zookeeperutil.h"
#include <arpa/inet.h>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <google/protobuf/descriptor.h>
#include <iostream>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace
{
  // 定义一个接收数据的缓冲区大小（1KB）
  const size_t kReceiveBufferSize = 1024;

  // 专门用来自动关闭 Socket 的资源守卫
  class SocketFdGuard
  {
  public:
    SocketFdGuard(int fd) : m_fd(fd) {}
    ~SocketFdGuard()
    {
      if (m_fd != -1)
      {
        close(m_fd);
      }
    }
    int get() const { return m_fd; }

  private:
    int m_fd;
  };

  // 构建一个错误消息字符串，包含操作名称、errno 错误码和对应的错误描述
  std::string BuildSocketErrorMessage(const std::string &action)
  {
    return action + " error! errno: " + std::to_string(errno) +
           " reason: " + std::string(strerror(errno));
  }

  // 设置 RPC 调用失败的错误信息，并记录日志
  void SetRpcFailure(google::protobuf::RpcController *controller,
                     const std::string &errorMessage)
  {
    LOG_ERROR("%s", errorMessage.c_str());
    controller->SetFailed(errorMessage);
  }

  // 从配置文件加载超时时间，单位为毫秒
  int LoadTimeoutMs(const std::string &key, int defaultValue)
  {
    const std::string value =
        MprpcApplication::GetInstance().GetConfig().Load(key);
    if (value.empty())
    {
      return defaultValue;
    }

    try
    {
      const int timeoutMs = std::stoi(value);
      return timeoutMs > 0 ? timeoutMs : defaultValue;
    }
    catch (...)
    {
      return defaultValue;
    }
  }

  //
  bool SetSocketTimeout(int socketFd, int optionName, int timeoutMs)
  // 你要设置的那个 Socket 句柄（就是你的网线插口）
  // 你要设置的那个选项，optionName：你要装哪种闹钟？通常传两个宏定义之一：SO_RCVTIMEO：接收（Read）超时闹钟。SO_SNDTIMEO：发送（Write）超时闹钟。
  // 你要设置的超时时间
  {
    /*
      timespec 的小水桶装的是纳秒（ns），10 亿纳秒 = 1 秒
      timeval 是 Linux 网络编程里更常用的结构体，它的小水桶装的是微秒（usec / μs），100 万微秒 = 1 秒。
      这是 C 语言历史遗留问题，不同的底层 API 喜欢用不同大小的“小水桶”
    */
    timeval timeout;
    timeout.tv_sec = timeoutMs / 1000;           // 毫秒/1000 = 整秒
    timeout.tv_usec = (timeoutMs % 1000) * 1000; // 毫秒%1000 = 毫秒的零头，乘以1000转换为微秒
    // setsockopt这是 Linux 提供的一个极其强大的底层函数，专门用来给 Socket 修改各种硬核配置
    // SOL_SOCKET：告诉操作系统：“我要修改的是最基本的 Socket 级别的属性”
    // &timeout, sizeof(timeout)：把我们刚刚装好水的 timeval 闹钟，原封不动地递给操作系统内核，让它帮我们把这个闹钟装到 Socket 上去
    return setsockopt(socketFd, SOL_SOCKET, optionName, &timeout,
                      sizeof(timeout)) == 0; // 函数成功返回0，失败返回-1
  }

  // 恢复 Socket 的原始标志（比如阻塞/非阻塞状态） 网路接口和原始状态快照
  bool RestoreSocketFlags(int socketFd, int originalFlags)
  {
    // fcntlLinux 底层控制文件（Socket 也是文件）属性的终极遥控器
    // F_SETFL：告诉操作系统：“我要修改的是文件的属性标志位”
    return fcntl(socketFd, F_SETFL, originalFlags) != -1; // 如果失败了统统返回 -1。如果不等于 -1，说明恢复成功
  }

  bool ConnectWithTimeout(int socketFd, struct sockaddr *serverAddress,
                          socklen_t addressLength, int timeoutMs)
  {
    // 记录 Socket 的原始标志（比如阻塞/非阻塞状态），以便后续恢复
    const int originalFlags = fcntl(socketFd, F_GETFL, 0);
    if (originalFlags == -1)
    {
      return false;
    }

    // 将 Socket 设置为非阻塞模式，以便我们可以使用 poll 来实现连接超时
    if (fcntl(socketFd, F_SETFL, originalFlags | O_NONBLOCK) == -1)
    {
      return false;
    }

    // connect socketFd与serverAddress建立连接，connect是一个系统调用，底层由操作系统内核实现
    if (connect(socketFd, serverAddress, addressLength) == 0)
    {
      // 如果连接成功，重置 Socket 的标志位为原始状态，并返回 false
      return RestoreSocketFlags(socketFd, originalFlags);
    }

    // 连接失败，检查是否是 EINPROGRESS 错误码，表示连接正在进行中（非阻塞模式下的正常情况）
    if (errno != EINPROGRESS)
    {
      const int savedErrno = errno;                // 1. 保护现场
      RestoreSocketFlags(socketFd, originalFlags); // 2. 打扫战场，将 Socket 的标志位恢复为原始状态
      errno = savedErrno;                          // 3. 恢复现场，系统调用发生失败就可能导致errno修改
      return false;
    }
    // errno == EINPROGRESS，说明连接正在进行中，我们需要使用 poll 来等待连接完成或者超时
    pollfd descriptor;
    descriptor.fd = socketFd;
    descriptor.events = POLLOUT; // POLLOUT（可写）TCP 完成三次握手，无论对方发没发数据过来，你的发送缓冲区就准备好了，这个 Socket 就变成了**“可写状态”**。
    descriptor.revents = 0;

    int pollResult = 0;
    do
    {
      pollResult = poll(&descriptor, 1, timeoutMs); // poll是一个系统调用，底层由操作系统内核实现。&descriptor：我们要监视的 Socket 事件的描述符数组以及只监听 POLLOUT 可写事件（这里只有一个）。1：描述符数组的长度。timeoutMs：等待事件发生的超时时间，单位是毫秒。
      /*
        返回值 > 0：好消息！门有动静了（Socket 变得可写或报错了）。
        返回值 == 0：闹钟响了，时间到了，门连个缝都没开（超时）。
        返回值 < 0 (通常是 -1)：坏消息！监控探头自己出了故障，或者遇到不可抗力报错了。
      */
    } while (pollResult == -1 && errno == EINTR); // pollResult == -1执行失败，errno = EINTR （死因：Interrupted system call，被系统信号打断了）

    // 执行失败了，或者超时了，统统算连接失败
    if (pollResult <= 0)
    {
      const int savedErrno = (pollResult == 0) ? ETIMEDOUT : errno; // 1. 保护现场：如果 pollResult == 0，说明是超时了，我们人为设置 errno 为 ETIMEDOUT；否则就是 poll 本身的错误，我们直接使用 errno 就行了。
      RestoreSocketFlags(socketFd, originalFlags);                  // 2. 打扫战场：不管是连接成功了还是失败了，我们都要把 Socket 的标志位恢复到原始状态，保持环境干净。
      errno = savedErrno;                                           // 3. 恢复现场，系统调用发生失败就可能导致errno修改 return false;
    }

    /*
      在非阻塞 connect 中，如果对方同意连接，Socket 会变成“可写（POLLOUT）”
      但是，如果对方残忍地拒绝了你（Connection Refused），或者路由不通，Socket 依然会变成“可写（POLLOUT）”！
    */
    int socketError = 0;
    socklen_t errorLength = sizeof(socketError);
    if (getsockopt(socketFd, SOL_SOCKET, SO_ERROR, &socketError, &errorLength) ==
        -1)
    { // == -1 表示 getsockopt 执行失败了，这时候我们也当做连接失败来处理
      const int savedErrno = errno;
      RestoreSocketFlags(socketFd, originalFlags);
      errno = savedErrno;
      return false;
    }
    // getsockopt 成功执行了，但 socketError 不为 0，说明连接过程中发生了错误（比如对方拒绝连接），我们也当做连接失败来处理
    if (socketError != 0)
    {
      RestoreSocketFlags(socketFd, originalFlags);
      errno = socketError;
      return false;
    }

    return RestoreSocketFlags(socketFd, originalFlags);
  }

  bool SendAll(int socketFd, const std::string &data)
  {
    size_t totalBytesSent = 0; // 已经发送的字节数
    while (totalBytesSent < data.size())
    {
      // send会返回实际发送的字节数，可能小于我们请求发送的字节数（data.size() - totalBytesSent），所以我们需要一个循环来确保把所有数据都发送出去。
      // data.data() + totalBytesSent：指向下一个要发送的数据位置的指针。data.size() - totalBytesSent：剩余要发送的数据字节数。
      const ssize_t currentBytesSent =
          send(socketFd, data.data() + totalBytesSent,
               data.size() - totalBytesSent, 0);
      // 避免 send 被系统信号打断了（返回 -1 且 errno == EINTR），我们在这种情况下继续发送，直到发送成功或者发生其他错误。
      if (currentBytesSent < 0 && errno == EINTR)
      {
        continue;
      }

      // <= 0 的情况说明发送失败了（可能是网络问题，或者对方关闭了连接），我们直接返回 false 来表示发送失败。
      if (currentBytesSent <= 0)
      {
        return false;
      }

      totalBytesSent += static_cast<size_t>(currentBytesSent);
    }

    return true;
  }

  bool ParseHostData(const std::string &hostData, std::string *ip,
                     uint16_t *port)
  {
    // size_type为find函数返回的类型，表示字符串中某个子串的位置，如果找不到子串就返回std::string::npos
    const std::string::size_type delimiterPos = hostData.find(':');
    if (delimiterPos == std::string::npos)
    {
      return false;
    }

    *ip = hostData.substr(0, delimiterPos);
    *port = static_cast<uint16_t>(std::stoi(hostData.substr(delimiterPos + 1)));
    return true;
  }
} // namespace

/*
    header_size + service_name + method_name + args_size + args
*/
// 重写父类的CallMethod方法
void MprpcChannel::CallMethod(const google::protobuf::MethodDescriptor *method,
                              google::protobuf::RpcController *controller,
                              const google::protobuf::Message *request,
                              google::protobuf::Message *response,
                              google::protobuf::Closure *done)
{
  // 防御性编程，防止用户忘记传controller
  assert(controller != nullptr && "Error: RpcController cannot be nullptr!");
  if (controller == nullptr)
  {
    return;
  }

  const google::protobuf::ServiceDescriptor *serviceDesc = method->service();

  const std::string serviceName = serviceDesc->name(); // 获取服务名
  const std::string methodName = method->name();       // 获取方法名

  // 获取参数的序列化字符串长度 args_size
  uint32_t argsSize = 0;
  std::string argsString;
  if (!request->SerializeToString(&argsString))
  {
    // 序列化失败
    SetRpcFailure(controller, "serialize request error! " + argsString);
    return;
  }
  argsSize = static_cast<uint32_t>(argsString.size());

  // 定义rpc的请求header
  mprpc::RpcHeader rpcHeader;
  rpcHeader.set_service_name(serviceName);
  rpcHeader.set_method_name(methodName);
  rpcHeader.set_args_size(argsSize);

  uint32_t headerSize = 0;
  std::string rpcHeaderString;
  if (!rpcHeader.SerializeToString(&rpcHeaderString))
  {
    // 序列化失败
    SetRpcFailure(controller,
                  "serialize rpc_header_str error! " + rpcHeaderString);
    return;
  }
  headerSize = static_cast<uint32_t>(rpcHeaderString.size());

  // 组织待发送的rpc请求的字符串
  std::string sendRpcString;
  // headerSize是一个32位无符号整数，表示rpcHeaderString的长度。htonl(headerSize)将这个长度转换为网络字节序，以确保在不同平台之间传输时能够正确解析。我们需要把这个转换后的headerSize作为前4个字节发送给对方，这样对方在接收时就知道接下来有多少字节是rpcHeaderString。
  const uint32_t networkHeaderSize = htonl(headerSize); // htonl是一个网络字节序转换函数，用于将主机字节序的整数转换为网络字节序（大端序）
  sendRpcString.insert(
      0, std::string(reinterpret_cast<const char *>(&networkHeaderSize), 4)); // reinterpret_cast<const char *>是让std::string把(&networkHeaderSize)看待为字符指针，4表示headerSize占4个字节
  sendRpcString += rpcHeaderString;                                           // rpc的请求头
  sendRpcString += argsString;                                                // rpc的请求参数

  // 打印一下组装好的请求信息（方便排查问题）
  std::cout << "============================================" << std::endl;
  std::cout << "header_size: " << headerSize << std::endl;
  std::cout << "rpc_header_str: " << rpcHeaderString << std::endl;
  std::cout << "service_name: " << serviceName << std::endl;
  std::cout << "method_name: " << methodName << std::endl;
  std::cout << "args_str: " << argsString << std::endl;
  std::cout << "============================================" << std::endl;

  // 使用TCP编程，完成rpc方法的远程调用
  // 创建socket
  const int clientfd = socket(AF_INET, SOCK_STREAM, 0);
  if (clientfd == -1)
  {
    SetRpcFailure(controller, BuildSocketErrorMessage("socket create"));
    return;
  }

  // 使用智能指针，当guard出了作用域，会自动关闭clientfd
  SocketFdGuard guard(clientfd);

  // 获取rpc服务提供者的ip地址
  // std::string ip =
  // MprpcApplication::GetInstance().GetConfig().Load("rpcserverip");
  // 获取rpc服务提供者的端口号 port
  // uint16_t port =
  // std::stoi(MprpcApplication::GetInstance().GetConfig().Load("rpcserverport").c_str());
  ZkClient zkclient;
  if (!zkclient.Start())
  {
    SetRpcFailure(controller, "connect zookeeper timeout or failed");
    return;
  }
  // /UserServiceRpc/Login
  const std::string methodPath = "/" + serviceName + "/" + methodName;
  // 127.0.0.1:8000
  const std::string hostData = zkclient.GetData(methodPath.c_str());
  if (hostData.empty())
  {
    SetRpcFailure(controller,
                  "get host data from zookeeper error! path: " + methodPath);
    return;
  }

  std::string ip;
  uint16_t port = 0;
  if (!ParseHostData(hostData, &ip, &port))
  {
    SetRpcFailure(controller, "invalid host data from zookeeper! path: " +
                                  methodPath + " data: " + hostData);
    return;
  }

  struct sockaddr_in serverAddress;
  std::memset(&serverAddress, 0, sizeof(serverAddress));
  serverAddress.sin_family = AF_INET; // IPv4 网络协议
  serverAddress.sin_port = htons(port);
  serverAddress.sin_addr.s_addr = inet_addr(ip.c_str());

  const int connectTimeoutMs = LoadTimeoutMs("rpcconnecttimeout", 3000);
  const int sendTimeoutMs = LoadTimeoutMs("rpcsendtimeout", 3000);
  const int recvTimeoutMs = LoadTimeoutMs("rpcrecvtimeout", 5000);

  // 连接rpc服务节点
  if (!ConnectWithTimeout(guard.get(),
                          reinterpret_cast<struct sockaddr *>(&serverAddress),
                          sizeof(serverAddress), connectTimeoutMs))
  {
    SetRpcFailure(controller, BuildSocketErrorMessage("connect"));
    return;
  }
  LOG_INFO("rpc connect success! service=%s method=%s target=%s:%u",
           serviceName.c_str(), methodName.c_str(), ip.c_str(), port);

  // 封装fd，设置发送超时时间
  if (!SetSocketTimeout(guard.get(), SO_SNDTIMEO, sendTimeoutMs))
  {
    SetRpcFailure(controller, BuildSocketErrorMessage("set send timeout"));
    return;
  }

  // 封装fd，设置接收超时时间
  if (!SetSocketTimeout(guard.get(), SO_RCVTIMEO, recvTimeoutMs))
  {
    SetRpcFailure(controller, BuildSocketErrorMessage("set recv timeout"));
    return;
  }

  // 发起rpc请求
  if (!SendAll(guard.get(), sendRpcString))
  {
    SetRpcFailure(controller, BuildSocketErrorMessage("send"));
    return;
  }

  // 接收rpc响应
  std::string responseString;
  char receiveBuffer[kReceiveBufferSize] = {0};
  while (true)
  {
    const ssize_t receivedBytes =
        recv(guard.get(), receiveBuffer, sizeof(receiveBuffer), 0);
    if (receivedBytes < 0 && errno == EINTR)
    {
      continue;
    }

    if (receivedBytes == 0)
    {
      break;
    }

    if (receivedBytes < 0)
    {
      SetRpcFailure(controller, BuildSocketErrorMessage("recv"));
      return;
    }

    responseString.append(receiveBuffer, static_cast<size_t>(receivedBytes));
  }

  if (responseString.empty())
  {
    SetRpcFailure(controller, "recv empty response from rpc provider");
    return;
  }

  if (!response->ParseFromString(responseString))
  {
    SetRpcFailure(controller, "parse error! response_str:" + responseString);
    return;
  }

  LOG_INFO("rpc call success! service=%s method=%s", serviceName.c_str(),
           methodName.c_str());

  if (done != nullptr)
  {
    done->Run();
  }
}
