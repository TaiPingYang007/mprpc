#pragma once
#include "google/protobuf/service.h"
#include <mymuduo/EventLoop.h>
#include <mymuduo/InetAddress.h>
#include <mymuduo/TcpConnection.h>
#include <mymuduo/TcpServer.h>
#include <string>
#include <unordered_map>

// 框架提供的专门服务发布rpc服务的网络对象类
class RpcProvider {
public:
  // 这里是框架开发，并不应该使用具体的rpc服务，而是使用所有rpc服务的基类
  // 这里是框架提供给外部使用的，可以发布rpc方法的函数接口
  void NotifyService(google::protobuf::Service *service);

  // 启动rpc服务节点，开始提供rpc远程网络调用服务
  void Run();

private:
  // 组合EventLoop
  EventLoop m_eventLoop;

  // service 服务类型信息
  struct ServiceInfo {
    google::protobuf::Service *m_service; // 保存服务对象
    std::unordered_map<std::string, const google::protobuf::MethodDescriptor *>
        m_methodMap; // 保存服务方法（服务方法名method_name，方法描述）
  };

  // 存储注册成功的服务对象和其服务方法的所有信息
  std::unordered_map<std::string, ServiceInfo>
      m_serviceInfoMap; // 保存服务(服务名字，对应服务的信息)

  // 新的socket连接回调
  void onConnection(const TcpConnectionPtr &conn);
  // 新的socket消息回调
  void onMessage(const TcpConnectionPtr &conn, Buffer *buffer, Timestamp time);
};