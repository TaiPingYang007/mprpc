#include "./include/mprpcchannel.h"
#include "rpcherder.pb.h"
#include "zookeeperutil.h"
#include <arpa/inet.h>
#include <cassert>
#include <cstring>
#include <google/protobuf/descriptor.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

// 专门用来自动关闭 Socket 的资源守卫
namespace {
class SocketFdGuard {
public:
  SocketFdGuard(int fd) : m_fd(fd) {}
  ~SocketFdGuard() {
    if (m_fd != -1)
      close(m_fd);
  }
  int get() const { return m_fd; }

private:
  int m_fd;
};
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
  const google::protobuf::ServiceDescriptor *service_desc = method->service();

  std::string service_name = service_desc->name(); // 获取服务名
  std::string method_name = method->name();        // 获取方法名

  // 获取参数的序列化字符串长度 args_size
  uint32_t args_size = 0;
  std::string args_str;
  if (request->SerializeToString(&args_str)) {
    // 序列化成功
    args_size = args_str.size();
  } else {
    // 序列化失败
    std::string err_info = "serialize request error!" + args_str;
    controller->SetFailed(err_info);
    return;
  }

  // 定义rpc的请求header
  mprpc::RpcHeader rpcHerader;
  rpcHerader.set_service_name(service_name);
  rpcHerader.set_method_name(method_name);
  rpcHerader.set_args_size(args_size);

  uint32_t header_size = 0;
  std::string rpc_header_str;
  if (rpcHerader.SerializeToString(&rpc_header_str)) {
    // 序列化成功
    header_size = rpc_header_str.size();
  } else {
    // 序列化失败
    std::string err_info = "serialize rpc_header_str error!" + rpc_header_str;
    controller->SetFailed(err_info);
    return;
  }

  // 组织待发送的rpc请求的字符串
  std::string send_rpc_str;
  send_rpc_str.insert(
      0, std::string((char *)&header_size, 4)); // 4字节的header_size
  send_rpc_str += rpc_header_str;               // rpc的请求头
  send_rpc_str += args_str;                     // rpc的请求参数

  // 打印一下组装好的请求信息（大厂规范：方便排查问题）
  std::cout << "============================================" << std::endl;
  std::cout << "header_size: " << header_size << std::endl;
  std::cout << "rpc_header_str: " << rpc_header_str << std::endl;
  std::cout << "service_name: " << service_name << std::endl;
  std::cout << "method_name: " << method_name << std::endl;
  std::cout << "args_str: " << args_str << std::endl;
  std::cout << "============================================" << std::endl;

  // 使用TCP编程，完成rpc方法的远程调用
  // 1、创建socket
  int clientfd = socket(AF_INET, SOCK_STREAM, 0);
  if (clientfd == -1) {
    std::string err_info =
        "socket create error! errno: " + std::to_string(errno);
    +"reason: " + std::string(strerror(errno));
    controller->SetFailed(err_info);
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
  std::string method_path = "/" + service_name + "/" + method_name;
  // 127.0.0.1:8000
  std::string host_data = zkclient.GetData(method_path.c_str());
  if (host_data == "") {
    std::string err_info = "get host data from zookeeper error! path: " + method_path;
    controller->SetFailed(err_info);
    return;
  }
  int idx = host_data.find(":");
  if (idx == -1) {
    std::string err_info = "invalid host data from zookeeper! path: " + method_path + " data: " + host_data;
    controller->SetFailed(err_info);
    return;
  }
  std::string ip = host_data.substr(0, idx);
  uint16_t port = std::stoi(host_data.substr(idx + 1));

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  server_addr.sin_addr.s_addr = inet_addr(ip.c_str());

  // 连接rpc服务节点
  if (connect(guard.get(), (struct sockaddr *)&server_addr,
              sizeof(server_addr)) == -1) {
    std::string err_info = "connect error! errno: " + std::to_string(errno) +
                           "reason: " + std::string(strerror(errno));
    controller->SetFailed(err_info);
    return;
  }

  // 发起rpc请求
  if (send(guard.get(), send_rpc_str.c_str(), send_rpc_str.size(), 0) == -1) {
    std::string err_info = "send error! errno: " + std::to_string(errno);
    +"reason: " + std::string(strerror(errno));
    controller->SetFailed(err_info);
    return;
  }

  // 接收rpc响应
  char recv_buf[1024] = {0};
  int recv_size = 0;
  if ((recv_size = recv(guard.get(), recv_buf, 1024, 0)) == -1) {
    std::string err_info = "recv error! errno: " + std::to_string(errno);
    +"reason: " + std::string(strerror(errno));
    controller->SetFailed(err_info);
    return;
  }

  // 反序列化rpc调用的响应数据
  std::string response_str(recv_buf, recv_size);
  if (!response->ParseFromString(response_str)) {
    std::string err_info = "parse error! response_str:" + response_str;
    controller->SetFailed(err_info);
    return;
  }
}