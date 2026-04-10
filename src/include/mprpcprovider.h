#pragma once
#include "mprpccontroller.h"
#include "zookeeperutil.h"
#include "google/protobuf/service.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <muduo/base/Timestamp.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpConnection.h>
#include <muduo/net/TcpServer.h>

// 框架提供的专门服务发布rpc服务的网络对象类
class RpcProvider {
public:
  // 这里是框架开发，并不应该使用具体的rpc服务，而是使用所有rpc服务的基类
  // 这里是框架提供给外部使用的，可以发布rpc方法的函数接口
  void NotifyService(google::protobuf::Service *service);

  // 启动rpc服务节点，开始提供rpc远程网络调用服务
  void Run();

private:
  // 组合了TcpServer
  std::unique_ptr<muduo::net::TcpServer> m_tcpServerPtr;
  // 保持 zk 会话常驻，避免临时节点注册后立刻失效
  std::unique_ptr<ZkClient> m_zkClientPtr;
  // 组合EventLoop
  muduo::net::EventLoop m_eventLoop;

  // service 服务类型信息
  struct ServiceInfo {
    google::protobuf::Service *m_service; // 保存服务对象
    std::unordered_map<std::string, const google::protobuf::MethodDescriptor *>
        m_methodMap; // 保存服务方法（服务方法名method_name，方法描述）
  };

  // 存储注册成功的服务对象和其服务方法的所有信息
  std::unordered_map<std::string, ServiceInfo>
      m_serviceInfoMap; // 保存服务(服务名字，对应服务的信息)

  struct RpcCallContext;

  bool RegisterServiceToZookeeper(const std::string &ip, uint16_t port);
  void ExecuteRpcRequest(const muduo::net::TcpConnectionPtr &conn,
                         const std::string &serviceName,
                         const std::string &methodName,
                         const std::string &argsString);
  // 新的socket连接回调
  void onConnection(const muduo::net::TcpConnectionPtr &conn);
  // 新的socket消息回调
  void onMessage(const muduo::net::TcpConnectionPtr &conn,
                 muduo::net::Buffer *buffer, muduo::Timestamp time);
  // Closure回调操作，用于序列化rpc的响应和网络发送
  void SendRpcResponse(const muduo::net::TcpConnectionPtr &conn,
                       RpcCallContext *callContext);
};
