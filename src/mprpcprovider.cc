#include "./include/mprpcprovider.h"
#include "./include/mprpcapplication.h"
#include "./include/zookeeperutil.h"
#include "rpcherder.pb.h"
#include <cstdio>
#include <cstring>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/service.h>
#include <google/protobuf/stubs/callback.h>
#include <iostream>

namespace {
const size_t kRpcHeaderSizeBytes = 4;
} // namespace

/**
ServiceInfo: 记录一个已注册服务的完整信息
  逻辑关系：
一个 Service 包含多个 Method。
我们不仅需要保存【服务对象】来调用方法，
还需要一个【方法映射表】通过字符串名字快速定位到具体的函数描述。
 */
// 这里是框架提供给外部使用的，可以发布rpc方法的函数接口

void RpcProvider::NotifyService(google::protobuf::Service *service) {
  // 获取服务对象的描述信息 需要#incldue <google/protobuf/descriptor.h>
  // ps:const也需要const接收

  ServiceInfo serviceInfo;
  const google::protobuf::ServiceDescriptor *serviceDesc =
      service->GetDescriptor();
  const std::string serviceName = serviceDesc->name();

  std::cout << "service_name:" << serviceName << std::endl;

  for (int i = 0; i < serviceDesc->method_count(); ++i) {
    // 获取了服务对象指定下标的服务方法的描述
    const google::protobuf::MethodDescriptor *methodDesc =
        serviceDesc->method(i);
    const std::string methodName = methodDesc->name();

    std::cout << "method_name:" << methodName << std::endl;

    serviceInfo.m_methodMap.insert(
        std::make_pair(methodName, methodDesc)); // 方法名字＋方法描述
  }

  serviceInfo.m_service = service;
  m_serviceInfoMap.insert(std::make_pair(serviceName, serviceInfo));
}

// 启动rpc服务节点，开始提供rpc远程网络调用服务
void RpcProvider::Run() {
  // 获取ip地址
  const std::string ip =
      MprpcApplication::GetInstance().GetConfig().Load("rpcserverip");
  const uint16_t port = static_cast<uint16_t>(
      std::stoi(MprpcApplication::GetInstance().GetConfig().Load(
          "rpcserverport")));
  muduo::net::InetAddress address(ip, port);

  // 创建TcpServer对象
  m_tcpServerPtr.reset(
      new muduo::net::TcpServer(&m_eventLoop, address, "RpcProvider"));

  // 绑定连接回调 分离了网络代码和业务代码
  m_tcpServerPtr->setConnectionCallback(
      [this](const muduo::net::TcpConnectionPtr &conn) { onConnection(conn); });
  // 绑定消息读写回调
  m_tcpServerPtr->setMessageCallback(
      [this](const muduo::net::TcpConnectionPtr &conn,
             muduo::net::Buffer *buffer, muduo::Timestamp time) {
        onMessage(conn, buffer, time);
      });

  // 设置muduo库的线程数量
  m_tcpServerPtr->setThreadNum(4);

  // 把当前rpc节点上要发布的的服务全部注册到zkserver上面，让rpc
  // client可以从zkserver上发现rpc server节点的服务信息
  // session timeout 30s zkclient和zkserver之间的心跳检测时间间隔10s zkclient和zkserver之间的连接断开后zkserver删除zkclient创建的临时节点
  // zkclient 网络I/O线程 1/3*timeout 时间发送ping消息
  RegisterServiceToZookeeper(ip, port);

  std::cout << "RpcProvider start service at ip:" << ip << " port:" << port
            << std::endl;

  // 启动server服务
  m_tcpServerPtr->start();
  m_eventLoop.loop();
}

void RpcProvider::RegisterServiceToZookeeper(const std::string &ip,
                                             uint16_t port) {
  ZkClient zkClient;
  zkClient.Start();

  // service_name 为永久性节点，method_name为临时性节点
  for (std::unordered_map<std::string, ServiceInfo>::const_iterator serviceIt =
           m_serviceInfoMap.begin();
       serviceIt != m_serviceInfoMap.end(); ++serviceIt) {
    const std::string servicePath = "/" + serviceIt->first;
    zkClient.Create(servicePath.c_str(), nullptr, 0);

    for (std::unordered_map<
             std::string,
             const google::protobuf::MethodDescriptor *>::const_iterator
             methodIt = serviceIt->second.m_methodMap.begin();
         methodIt != serviceIt->second.m_methodMap.end(); ++methodIt) {
      const std::string methodPath = servicePath + "/" + methodIt->first;
      char methodPathData[128] = {0};
      std::snprintf(methodPathData, sizeof(methodPathData), "%s:%u",
                    ip.c_str(), port);
      zkClient.Create(methodPath.c_str(), methodPathData,
                      static_cast<int>(std::strlen(methodPathData)),
                      ZOO_EPHEMERAL);
    }
  }
}

// 新的socket连接回调
void RpcProvider::onConnection(const muduo::net::TcpConnectionPtr &conn) {
  if (!conn->connected()) {
    // 和rpc client连接断开
    conn->shutdown();
  }
}

/*
在框架内部，RpcProvider和RpcConsumer协商好之间通信用的protobuf数据类型
service_name method_name args
定义proto的message类型进行数据头（service_name、method_name以及args_size防止粘包）的序列化和反序列化

header_size（4字节二进制存储，数据头长度） + header_str + args_str
4字节的header_size 是二进制存储
*/
// 新的socket消息回调 如果远端有一个rpc服务的请求，那么onMessage方法就会响应
void RpcProvider::onMessage(const muduo::net::TcpConnectionPtr &conn,
                            muduo::net::Buffer *buffer,
                            muduo::Timestamp time) {
  (void)time;
  // 网络字节流是不保证边界的（TCP拆包/粘包现象）。
  // 必须根据协议中设计的消息包结构（首部长度 + RpcHeader +
  // 参数体）逐步读取和校验长度。

  // 1. 判断是否接收到了足够的 header_size 字节（4个字节）
  if (buffer->readableBytes() < kRpcHeaderSizeBytes) {
    return; // 数据不够，直接返回，等待下一次 epoll 唤醒 onMessage
  }

  uint32_t header_size = 0;
  // 直接从 Buffer 的底层数组 peek，不要用 string 进行拷贝以防越界
  std::memcpy(&header_size, buffer->peek(), kRpcHeaderSizeBytes);

  // 2. 根据 header_size 判断 header_str（Protobuf 序列化数据头）是否完整到达
  if (buffer->readableBytes() <
      kRpcHeaderSizeBytes + static_cast<size_t>(header_size)) {
    return;
  }

  // 读取 header_str，这才是安全的做法
  const std::string rpcHeaderString(
      buffer->peek() + kRpcHeaderSizeBytes,
      header_size); // std::string(const char* s, size_t n) 构造函数
  mprpc::RpcHeader rpcHeader;
  if (!rpcHeader.ParseFromString(rpcHeaderString)) {
    // 数据头反序列化失败
    std::cout << "rpc_header_str:" << rpcHeaderString << " parse error!"
              << std::endl;
    conn->shutdown();
    return;
  }

  const std::string serviceName = rpcHeader.service_name();
  const std::string methodName = rpcHeader.method_name();
  const uint32_t argsSize = rpcHeader.args_size();

  // 3. 判断整个包（数据头4字节 + RpcHeader长度 + 参数体长度）是否已经完整到达
  if (buffer->readableBytes() <
      kRpcHeaderSizeBytes + static_cast<size_t>(header_size) + argsSize) {
    return; // 数据没收全，继续等！
  }

  // 4. 一个完整的 RPC 请求消息已经安全抵达！正式提取并消耗缓冲区！
  // 使用 retrieve 丢弃已经被处理掉的 header 部分数据
  buffer->retrieve(kRpcHeaderSizeBytes + header_size);

  // 按照 args_size 取出剩下的参数体字符串
  const std::string argsString = buffer->retrieveAsString(argsSize);

  // 打印调试信息
  std::cout << "========================================" << std::endl;
  std::cout << "service_name:" << serviceName << std::endl;
  std::cout << "method_name:" << methodName << std::endl;
  std::cout << "args_size:" << argsSize << std::endl;
  std::cout << "args_str:" << argsString << std::endl;
  std::cout << "========================================" << std::endl;

  ExecuteRpcRequest(conn, serviceName, methodName, argsString);
}

void RpcProvider::ExecuteRpcRequest(
    const muduo::net::TcpConnectionPtr &conn, const std::string &serviceName,
    const std::string &methodName, const std::string &argsString) {
  // 获取service对象和method对象
  std::unordered_map<std::string, ServiceInfo>::const_iterator serviceIt =
      m_serviceInfoMap.find(serviceName);
  if (serviceIt == m_serviceInfoMap.end()) {
    std::cout << "service_name:" << serviceName << " is not exist!"
              << std::endl;
    return;
  }

  std::unordered_map<std::string,
                     const google::protobuf::MethodDescriptor *>::const_iterator
      methodIt = serviceIt->second.m_methodMap.find(methodName);
  if (methodIt == serviceIt->second.m_methodMap.end()) {
    std::cout << "method_name:" << methodName << " is not exist!"
              << std::endl;
    return;
  }

  google::protobuf::Service *service = serviceIt->second.m_service; // 获取服务对象
  const google::protobuf::MethodDescriptor *method =
      methodIt->second; // 获取方法对象

  // 生成rpc方法调用的请求request参数和响应response
  std::unique_ptr<google::protobuf::Message> requestMessage(
      service->GetRequestPrototype(method).New());
  if (!requestMessage->ParseFromString(argsString)) {
    std::cout << "request parse error, content:" << argsString << std::endl;
    return;
  }

  std::unique_ptr<google::protobuf::Message> responseMessage(
      service->GetResponsePrototype(method).New());

  // 给下面的method方法的调用，绑定一个Closure回调函数
  ::google::protobuf::Closure *done =
      google::protobuf::NewCallback<RpcProvider,
                                    const muduo::net::TcpConnectionPtr &,
                                    google::protobuf::Message *>(
          this, &RpcProvider::SendRpcResponse, conn, responseMessage.get());

  // 在框架上根据远端rpc请求，调用rpc节点上发布的方法
  // new UserService.Login(contorller,request,response,done)
  service->CallMethod(method, nullptr, requestMessage.get(),
                      responseMessage.get(), done);
  responseMessage.release();
}

// Closure回调操作，用于序列化rpc的响应和网络发送
void RpcProvider::SendRpcResponse(const muduo::net::TcpConnectionPtr &conn,
                                  google::protobuf::Message *response) {
  std::unique_ptr<google::protobuf::Message> responseGuard(response);
  std::string response_str;
  if (responseGuard->SerializeToString(&response_str)) {
    // 序列化成功后，通过网络把rpc方法执行的结果发送回rpc的调用方
    conn->send(response_str);
  } else {
    // 序列化失败
    std::cout << "response serialize error!" << std::endl;
  }
  // 模拟http的短链接服务，由rpcprovider主动断开连接
  conn->shutdown();
}
