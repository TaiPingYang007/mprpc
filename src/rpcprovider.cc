#include "./include/rpcprovider.h"
#include "./include/mprpcapplication.h"
#include <google/protobuf/descriptor.h>
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
  // 获取服务对象的方法的数量
  int method_count = pserviceDesc->method_count();

  for (int i = 0; i < method_count; ++i) {
    // 获取了服务对象指定下标的服务方法的描述
    const google::protobuf::MethodDescriptor *pmethodDesc =
        pserviceDesc->method(i);

    // 获取服务方法的名字
    std::string method_name = pmethodDesc->name();

    service_info.m_methodMap.insert(
        {method_name, pmethodDesc}); // 方法名字＋方法描述
  }
  service_info.m_service = service;
  m_serviceInfoMap.insert({service_name, service_info});
}

// 启动rpc服务节点，开始提供rpc远程网络调用服务
void RpcProvider::Run() {
  // 组合了TcpServer
  std::unique_ptr<TcpServer> m_tcpserverPtr;

  // 过去ip地址
  std::string ip =
      MprpcApplication::GetInstance().GetConfig().Load("rpcserverip");
  // 获取端口号 port
  uint16_t port = atoi(MprpcApplication::GetInstance()
                           .GetConfig()
                           .Load("rpcserverport")
                           .c_str());
  InetAddress address(port, ip);

  // 创建TcpServer对象
  TcpServer server(&m_eventLoop, address, "RpcProvider");

  // 绑定连接回调 分离了网络代码和业务代码
  server.setConnectionCallback(
      [this](const TcpConnectionPtr &conn) { onConnection(conn); });
  // 绑定消息读写回调
  server.setMessageCallback(
      [this](const TcpConnectionPtr &conn, Buffer *buffer, Timestamp time) {
        onMessage(conn, buffer, time);
      });

  // 设置muduo库的线程数量
  server.setThreadNum(4);

  std ::cout << "RpcProvider start service at ip:" << ip << " port:" << port
             << std ::endl;

  // 启动server服务
  server.start();
  m_eventLoop.loop();
}

// 新的socket连接回调
void RpcProvider::onConnection(const TcpConnectionPtr &conn) {}

// 新的socket消息回调
void RpcProvider::onMessage(const TcpConnectionPtr &conn, Buffer *buffer,
                            Timestamp time) {}