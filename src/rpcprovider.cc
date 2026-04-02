#include "./include/rpcprovider.h"
#include "./include/mprpcapplication.h"
#include "rpcherder.pb.h"
#include <google/protobuf/descriptor.h>
#include <google/protobuf/service.h>
#include <google/protobuf/stubs/callback.h>
#include <memory>
#include <mymuduo/TcpServer.h>

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

  ServiceInfo service_info;

  const google::protobuf::ServiceDescriptor *pserviceDesc =
      service->GetDescriptor();

  // 获取服务的名字
  std::string service_name = pserviceDesc->name();

  std::cout << "service_name:" << service_name << std::endl;

  // 获取服务对象的方法的数量
  int method_count = pserviceDesc->method_count();

  for (int i = 0; i < method_count; ++i) {
    // 获取了服务对象指定下标的服务方法的描述
    const google::protobuf::MethodDescriptor *pmethodDesc =
        pserviceDesc->method(i);

    // 获取服务方法的名字
    std::string method_name = pmethodDesc->name();

    std::cout << "method_name:" << method_name << std::endl;

    service_info.m_methodMap.insert(
        {method_name, pmethodDesc}); // 方法名字＋方法描述
  }
  service_info.m_service = service;
  m_serviceInfoMap.insert({service_name, service_info});
}

// 启动rpc服务节点，开始提供rpc远程网络调用服务
void RpcProvider::Run() {
  // 获取ip地址
  std::string ip =
      MprpcApplication::GetInstance().GetConfig().Load("rpcserverip");
  // 获取端口号 port
  uint16_t port = std::stoi(MprpcApplication::GetInstance()
                                .GetConfig()
                                .Load("rpcserverport")
                                .c_str());
  InetAddress address(port, ip);

  // 创建TcpServer对象
  m_tcpserverPtr =
      std::make_unique<TcpServer>(&m_eventLoop, address, "RpcProvider");

  // 绑定连接回调 分离了网络代码和业务代码
  m_tcpserverPtr->setConnectionCallback(
      [this](const TcpConnectionPtr &conn) { onConnection(conn); });
  // 绑定消息读写回调
  m_tcpserverPtr->setMessageCallback(
      [this](const TcpConnectionPtr &conn, Buffer *buffer, Timestamp time) {
        onMessage(conn, buffer, time);
      });

  // 设置muduo库的线程数量
  m_tcpserverPtr->setThreadNum(4);

  std ::cout << "RpcProvider start service at ip:" << ip << " port:" << port
             << std ::endl;

  // 启动server服务
  m_tcpserverPtr->start();
  m_eventLoop.loop();
}

// 新的socket连接回调
void RpcProvider::onConnection(const TcpConnectionPtr &conn) {
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
void RpcProvider::onMessage(const TcpConnectionPtr &conn, Buffer *buffer,
                            Timestamp time) {
  // 网络字节流是不保证边界的（TCP拆包/粘包现象）。
  // 必须根据协议中设计的消息包结构（首部长度 + RpcHeader +
  // 参数体）逐步读取和校验长度。

  // 1. 判断是否接收到了足够的 header_size 字节（4个字节）
  if (buffer->readableBytes() < 4) {
    return; // 数据不够，直接返回，等待下一次 epoll 唤醒 onMessage
  }

  uint32_t header_size = 0;
  // 直接从 Buffer 的底层数组 peek，不要用 string 进行拷贝以防越界
  memcpy(&header_size, buffer->peek(), 4);

  // 2. 根据 header_size 判断 header_str（Protobuf 序列化数据头）是否完整到达
  if (buffer->readableBytes() < 4 + header_size) {
    return;
  }

  // 读取 header_str，这才是安全的做法
  std::string rpc_header_str(
      buffer->peek() + 4,
      header_size); // std::string(const char* s, size_t n) 构造函数
  mprpc::RpcHeader rpcHeader;

  std::string service_name;
  std::string method_name;
  uint32_t args_size = 0;

  if (rpcHeader.ParseFromString(rpc_header_str)) {
    // 数据头反序列化成功
    service_name = rpcHeader.service_name();
    method_name = rpcHeader.method_name();
    args_size = rpcHeader.args_size();
  } else {
    // 数据头反序列化失败
    std::cout << "rpc_header_str:" << rpc_header_str << " parse error!"
              << std::endl;
    // 线上环境如果解析失败，这里一定要断开连接防止恶意攻击
    // conn->shutdown();
    return;
  }

  // 3. 判断整个包（数据头4字节 + RpcHeader长度 + 参数体长度）是否已经完整到达
  if (buffer->readableBytes() < 4 + header_size + args_size) {
    return; // 数据没收全，继续等！
  }

  // 4. 一个完整的 RPC 请求消息已经安全抵达！正式提取并消耗缓冲区！
  // 使用 retrieve 丢弃已经被处理掉的 header 部分数据
  buffer->retrieve(4 + header_size);

  // 按照 args_size 取出剩下的参数体字符串
  std::string args_str = buffer->retrieveAsString(args_size);

  // 打印调试信息
  std::cout << "========================================" << std::endl;
  std::cout << "service_name:" << service_name << std::endl;
  std::cout << "method_name:" << method_name << std::endl;
  std::cout << "args_size:" << args_size << std::endl;
  std::cout << "args_str:" << args_str << std::endl;
  std::cout << "========================================" << std::endl;

  // 获取service对象和method对象
  auto it = m_serviceInfoMap.find(service_name);
  if (it == m_serviceInfoMap.end()) {
    std::cout << "service_name:" << service_name << " is not exist!"
              << std::endl;
    return;
  }

  auto method_it = it->second.m_methodMap.find(method_name);
  if (method_it == it->second.m_methodMap.end()) {
    std::cout << "method_name:" << method_name << " is not exist!" << std::endl;
    return;
  }

  google::protobuf::Service *service = it->second.m_service; // 获取服务对象
  const google::protobuf::MethodDescriptor *method =
      method_it->second; // 获取方法对象

  // 生成rpc方法调用的请求request参数和响应response
  google::protobuf::Message *request =
      service->GetRequestPrototype(method).New();

  if (!request->ParseFromString(args_str)) {
    std::cout << "request parse error， content:" << args_str << std::endl;
    delete request;
    return;
  }

  google::protobuf::Message *response =
      service->GetResponsePrototype(method).New();

  // 给下面的method方法的调用，绑定一个Closure回调函数
  ::google::protobuf::Closure *done =
      google::protobuf::NewCallback<RpcProvider, const TcpConnectionPtr &,
                                    google::protobuf::Message *>(
          this, &RpcProvider::SendRpcResponse, conn, response);

  // 在框架上根据远端rpc请求，调用rpc节点上发布的方法
  // new UserService.Login(contorller,request,response,done)
  service->CallMethod(method, nullptr, request, response, done);

  // 释放request
  delete request;
}

// Closure回调操作，用于序列化rpc的响应和网络发送
void RpcProvider::SendRpcResponse(const TcpConnectionPtr &conn,
                                  google::protobuf::Message *response) {
  std::string response_str;
  if (response->SerializeToString(&response_str)) {
    // 序列化成功后，通过网络把rpc方法执行的结果发送回rpc的调用方
    conn->send(response_str);
  } else {
    // 序列化失败
    std::cout << "response serialize error!" << std::endl;
  }
  // 模拟http的短链接服务，由rpcprovider主动断开连接
  conn->shutdown();
  // 释放response
  delete response;
}