#include "./include/mprpcchannel.h"
#include "rpcherder.pb.h"
#include "zookeeperutil.h"
#include <arpa/inet.h>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <google/protobuf/descriptor.h>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

// 专门用来自动关闭 Socket 的资源守卫
namespace {
const size_t kReceiveBufferSize = 1024;

class SocketFdGuard {
public:
  SocketFdGuard(int fd) : m_fd(fd) {}
  ~SocketFdGuard() {
    if (m_fd != -1) {
      close(m_fd);
    }
  }
  int get() const { return m_fd; }

private:
  int m_fd;
};

std::string BuildSocketErrorMessage(const std::string &action) {
  return action + " error! errno: " + std::to_string(errno) +
         " reason: " + std::string(strerror(errno));
}

bool SendAll(int socketFd, const std::string &data) {
  size_t totalBytesSent = 0;
  while (totalBytesSent < data.size()) {
    const ssize_t currentBytesSent =
        send(socketFd, data.data() + totalBytesSent,
             data.size() - totalBytesSent, 0);
    if (currentBytesSent <= 0) {
      return false;
    }

    totalBytesSent += static_cast<size_t>(currentBytesSent);
  }

  return true;
}

bool ParseHostData(const std::string &hostData, std::string *ip,
                   uint16_t *port) {
  const std::string::size_type delimiterPos = hostData.find(':');
  if (delimiterPos == std::string::npos) {
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
                              google::protobuf::Closure *done) {
  // 防御性编程，防止用户忘记传controller
  assert(controller != nullptr && "Error: RpcController cannot be nullptr!");
  if (controller == nullptr) {
    return;
  }

  const google::protobuf::ServiceDescriptor *serviceDesc = method->service();

  const std::string serviceName = serviceDesc->name(); // 获取服务名
  const std::string methodName = method->name();       // 获取方法名

  // 获取参数的序列化字符串长度 args_size
  uint32_t argsSize = 0;
  std::string argsString;
  if (!request->SerializeToString(&argsString)) {
    // 序列化失败
    controller->SetFailed("serialize request error! " + argsString);
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
  if (!rpcHeader.SerializeToString(&rpcHeaderString)) {
    // 序列化失败
    controller->SetFailed("serialize rpc_header_str error! " + rpcHeaderString);
    return;
  }
  headerSize = static_cast<uint32_t>(rpcHeaderString.size());

  // 组织待发送的rpc请求的字符串
  std::string sendRpcString;
  sendRpcString.insert(0, std::string(reinterpret_cast<char *>(&headerSize), 4));
  sendRpcString += rpcHeaderString; // rpc的请求头
  sendRpcString += argsString;      // rpc的请求参数

  // 打印一下组装好的请求信息（大厂规范：方便排查问题）
  std::cout << "============================================" << std::endl;
  std::cout << "header_size: " << headerSize << std::endl;
  std::cout << "rpc_header_str: " << rpcHeaderString << std::endl;
  std::cout << "service_name: " << serviceName << std::endl;
  std::cout << "method_name: " << methodName << std::endl;
  std::cout << "args_str: " << argsString << std::endl;
  std::cout << "============================================" << std::endl;

  // 使用TCP编程，完成rpc方法的远程调用
  // 1、创建socket
  const int clientfd = socket(AF_INET, SOCK_STREAM, 0);
  if (clientfd == -1) {
    controller->SetFailed(BuildSocketErrorMessage("socket create"));
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
  zkclient.Start();
  // /UserServiceRpc/Login
  const std::string methodPath = "/" + serviceName + "/" + methodName;
  // 127.0.0.1:8000
  const std::string hostData = zkclient.GetData(methodPath.c_str());
  if (hostData.empty()) {
    controller->SetFailed("get host data from zookeeper error! path: " +
                          methodPath);
    return;
  }

  std::string ip;
  uint16_t port = 0;
  if (!ParseHostData(hostData, &ip, &port)) {
    controller->SetFailed("invalid host data from zookeeper! path: " +
                          methodPath + " data: " + hostData);
    return;
  }

  struct sockaddr_in serverAddress;
  std::memset(&serverAddress, 0, sizeof(serverAddress));
  serverAddress.sin_family = AF_INET;
  serverAddress.sin_port = htons(port);
  serverAddress.sin_addr.s_addr = inet_addr(ip.c_str());

  // 连接rpc服务节点
  if (connect(guard.get(), reinterpret_cast<struct sockaddr *>(&serverAddress),
              sizeof(serverAddress)) == -1) {
    controller->SetFailed(BuildSocketErrorMessage("connect"));
    return;
  }

  // 发起rpc请求
  if (!SendAll(guard.get(), sendRpcString)) {
    controller->SetFailed(BuildSocketErrorMessage("send"));
    return;
  }

  // 接收rpc响应
  std::string responseString;
  char receiveBuffer[kReceiveBufferSize] = {0};
  while (true) {
    const ssize_t receivedBytes =
        recv(guard.get(), receiveBuffer, sizeof(receiveBuffer), 0);
    if (receivedBytes == 0) {
      break;
    }

    if (receivedBytes < 0) {
      controller->SetFailed(BuildSocketErrorMessage("recv"));
      return;
    }

    responseString.append(receiveBuffer, static_cast<size_t>(receivedBytes));
  }

  if (responseString.empty()) {
    controller->SetFailed("recv empty response from rpc provider");
    return;
  }

  if (!response->ParseFromString(responseString)) {
    controller->SetFailed("parse error! response_str:" + responseString);
    return;
  }

  if (done != nullptr) {
    done->Run();
  }
}
