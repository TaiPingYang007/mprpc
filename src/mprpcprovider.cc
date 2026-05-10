#include "./include/mprpcprovider.h"
#include "./include/logger.h"
#include "./include/mprpcapplication.h"
#include "./include/zookeeperutil.h"
#include "rpcherder.pb.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/service.h>
#include <google/protobuf/stubs/callback.h>
#include <sstream>
#include <vector>

namespace {
const size_t kRpcHeaderSizeBytes = 4;

int LoadInt(const std::string &key, int defaultValue) {
  const std::string value = MprpcApplication::GetConfig().Load(key);
  if (value.empty()) {
    return defaultValue;
  }

  try {
    const int parsed = std::stoi(value);
    return parsed > 0 ? parsed : defaultValue;
  } catch (...) {
    return defaultValue;
  }
}

uint16_t LoadPort(const std::string &key, uint16_t defaultValue) {
  return static_cast<uint16_t>(LoadInt(key, defaultValue));
}

std::string NormalizeNamespace(const std::string &ns) {
  std::string value = ns;
  while (!value.empty() && value.front() == '/') {
    value.erase(value.begin());
  }
  while (!value.empty() && value.back() == '/') {
    value.pop_back();
  }
  return value.empty() ? std::string("mprpc") : value;
}

std::string BuildRootPath() {
  return "/" + NormalizeNamespace(
                     MprpcApplication::GetConfig().Load("MPRPC_ZK_NAMESPACE"));
}

std::string BuildServicePath(const std::string &serviceName) {
  return BuildRootPath() + "/" + serviceName;
}

std::string BuildMethodPath(const std::string &serviceName,
                            const std::string &methodName) {
  return BuildServicePath(serviceName) + "/" + methodName;
}

std::string BuildProviderPath(const std::string &serviceName,
                              const std::string &methodName) {
  return BuildMethodPath(serviceName, methodName) + "/providers";
}

std::vector<std::string> BuildPathPrefixes(const std::string &path) {
  std::vector<std::string> prefixes;
  std::stringstream ss(path);
  std::string segment;
  std::string current;
  while (std::getline(ss, segment, '/')) {
    if (segment.empty()) {
      continue;
    }
    current += "/";
    current += segment;
    prefixes.push_back(current);
  }
  return prefixes;
}

bool EnsurePersistentPath(ZkClient *client, const std::string &path) {
  const std::vector<std::string> prefixes = BuildPathPrefixes(path);
  for (std::vector<std::string>::const_iterator it = prefixes.begin();
       it != prefixes.end(); ++it) {
    if (!client->Create(it->c_str(), nullptr, 0, 0)) {
      return false;
    }
  }
  return true;
}
} // namespace

struct RpcProvider::RpcCallContext {
  std::unique_ptr<google::protobuf::Message> request;
  std::unique_ptr<google::protobuf::Message> response;
  std::unique_ptr<MprpcController> controller;
};

void RpcProvider::NotifyService(google::protobuf::Service *service) {
  ServiceInfo serviceInfo;
  const google::protobuf::ServiceDescriptor *serviceDesc =
      service->GetDescriptor();
  const std::string serviceName = serviceDesc->name();
  LOG_INFO("register service: %s", serviceName.c_str());

  for (int i = 0; i < serviceDesc->method_count(); ++i) {
    const google::protobuf::MethodDescriptor *methodDesc =
        serviceDesc->method(i);
    const std::string methodName = methodDesc->name();
    LOG_INFO("register method: %s::%s", serviceName.c_str(),
             methodName.c_str());
    serviceInfo.m_methodMap.insert(std::make_pair(methodName, methodDesc));
  }

  serviceInfo.m_service = service;
  m_serviceInfoMap.insert(std::make_pair(serviceName, serviceInfo));
}

void RpcProvider::Run() {
  const std::string bindIp = MprpcApplication::GetConfig().Load("RPC_BIND_IP");
  const uint16_t port = LoadPort("RPC_PORT", 8000);
  std::string advertiseHost =
      MprpcApplication::GetConfig().Load("RPC_ADVERTISE_HOST");
  if (advertiseHost.empty()) {
    advertiseHost = bindIp;
  }

  const int threadNum = LoadInt("RPC_IO_THREADS", 4);
  muduo::net::InetAddress address(bindIp, port);

  m_tcpServerPtr.reset(
      new muduo::net::TcpServer(&m_eventLoop, address, "RpcProvider"));
  m_tcpServerPtr->setConnectionCallback(
      [this](const muduo::net::TcpConnectionPtr &conn) { onConnection(conn); });
  m_tcpServerPtr->setMessageCallback(
      [this](const muduo::net::TcpConnectionPtr &conn,
             muduo::net::Buffer *buffer, muduo::Timestamp time) {
        onMessage(conn, buffer, time);
      });
  m_tcpServerPtr->setThreadNum(threadNum);

  if (!RegisterServiceToZookeeper(advertiseHost, port)) {
    LOG_ERROR("register service to zookeeper failed! advertise=%s port=%u",
              advertiseHost.c_str(), port);
    return;
  }

  LOG_INFO("rpc provider start bind=%s:%u advertise=%s:%u threads=%d",
           bindIp.c_str(), port, advertiseHost.c_str(), port, threadNum);

  m_tcpServerPtr->start();
  m_eventLoop.loop();
}

bool RpcProvider::RegisterServiceToZookeeper(const std::string &advertiseHost,
                                             uint16_t port) {
  if (m_zkClientPtr == nullptr) {
    m_zkClientPtr.reset(new ZkClient());
  }

  if (!m_zkClientPtr->Start(true)) {
    return false;
  }

  const std::string hostData =
      advertiseHost + ":" + std::to_string(static_cast<unsigned int>(port));
  for (std::unordered_map<std::string, ServiceInfo>::const_iterator serviceIt =
           m_serviceInfoMap.begin();
       serviceIt != m_serviceInfoMap.end(); ++serviceIt) {
    for (std::unordered_map<
             std::string,
             const google::protobuf::MethodDescriptor *>::const_iterator
             methodIt = serviceIt->second.m_methodMap.begin();
         methodIt != serviceIt->second.m_methodMap.end(); ++methodIt) {
      const std::string providerPath =
          BuildProviderPath(serviceIt->first, methodIt->first);
      if (!EnsurePersistentPath(m_zkClientPtr.get(), providerPath)) {
        return false;
      }

      const std::string providerPrefix = providerPath + "/provider-";
      const std::string createdPath = m_zkClientPtr->CreateSequential(
          providerPrefix.c_str(), hostData.c_str(),
          static_cast<int>(hostData.size()), ZOO_EPHEMERAL);
      if (createdPath.empty()) {
        return false;
      }

      LOG_INFO("register rpc provider node service=%s method=%s path=%s target=%s",
               serviceIt->first.c_str(), methodIt->first.c_str(),
               createdPath.c_str(), hostData.c_str());
    }
  }

  return true;
}

void RpcProvider::onConnection(const muduo::net::TcpConnectionPtr &conn) {
  if (!conn->connected()) {
    conn->shutdown();
  }
}

void RpcProvider::onMessage(const muduo::net::TcpConnectionPtr &conn,
                            muduo::net::Buffer *buffer,
                            muduo::Timestamp time) {
  (void)time;
  if (buffer->readableBytes() < kRpcHeaderSizeBytes) {
    return;
  }

  uint32_t headerSize = 0;
  std::memcpy(&headerSize, buffer->peek(), kRpcHeaderSizeBytes);
  headerSize = ntohl(headerSize);
  if (buffer->readableBytes() <
      kRpcHeaderSizeBytes + static_cast<size_t>(headerSize)) {
    return;
  }

  const std::string rpcHeaderString(buffer->peek() + kRpcHeaderSizeBytes,
                                    headerSize);
  mprpc::RpcHeader rpcHeader;
  if (!rpcHeader.ParseFromString(rpcHeaderString)) {
    LOG_ERROR("rpc header parse error");
    conn->shutdown();
    return;
  }

  const std::string serviceName = rpcHeader.service_name();
  const std::string methodName = rpcHeader.method_name();
  const uint32_t argsSize = rpcHeader.args_size();

  if (buffer->readableBytes() <
      kRpcHeaderSizeBytes + static_cast<size_t>(headerSize) + argsSize) {
    return;
  }

  buffer->retrieve(kRpcHeaderSizeBytes + headerSize);
  const std::string argsString = buffer->retrieveAsString(argsSize);

  LOG_INFO("rpc request received service=%s method=%s args_size=%u",
           serviceName.c_str(), methodName.c_str(), argsSize);
  ExecuteRpcRequest(conn, serviceName, methodName, argsString);
}

void RpcProvider::ExecuteRpcRequest(
    const muduo::net::TcpConnectionPtr &conn, const std::string &serviceName,
    const std::string &methodName, const std::string &argsString) {
  std::unordered_map<std::string, ServiceInfo>::const_iterator serviceIt =
      m_serviceInfoMap.find(serviceName);
  if (serviceIt == m_serviceInfoMap.end()) {
    LOG_ERROR("rpc service not exist! service=%s", serviceName.c_str());
    return;
  }

  std::unordered_map<std::string,
                     const google::protobuf::MethodDescriptor *>::const_iterator
      methodIt = serviceIt->second.m_methodMap.find(methodName);
  if (methodIt == serviceIt->second.m_methodMap.end()) {
    LOG_ERROR("rpc method not exist! service=%s method=%s", serviceName.c_str(),
              methodName.c_str());
    return;
  }

  google::protobuf::Service *service = serviceIt->second.m_service;
  const google::protobuf::MethodDescriptor *method = methodIt->second;

  std::unique_ptr<RpcCallContext> callContext(new RpcCallContext());
  callContext->request.reset(service->GetRequestPrototype(method).New());
  if (!callContext->request->ParseFromString(argsString)) {
    LOG_ERROR("request parse error! service=%s method=%s", serviceName.c_str(),
              methodName.c_str());
    return;
  }

  callContext->response.reset(service->GetResponsePrototype(method).New());
  callContext->controller.reset(new MprpcController());

  ::google::protobuf::Closure *done =
      google::protobuf::NewCallback<RpcProvider,
                                    const muduo::net::TcpConnectionPtr &,
                                    RpcCallContext *>(
          this, &RpcProvider::SendRpcResponse, conn, callContext.get());

  service->CallMethod(method, callContext->controller.get(),
                      callContext->request.get(), callContext->response.get(),
                      done);
  callContext.release();
}

void RpcProvider::SendRpcResponse(const muduo::net::TcpConnectionPtr &conn,
                                  RpcCallContext *callContext) {
  std::unique_ptr<RpcCallContext> contextGuard(callContext);

  if (contextGuard->controller->Failed()) {
    LOG_ERROR("rpc response controller error: %s",
              contextGuard->controller->ErrorText().c_str());
  }

  std::string responseString;
  if (contextGuard->response->SerializeToString(&responseString)) {
    conn->send(responseString);
  } else {
    LOG_ERROR("response serialize error!");
  }
  conn->shutdown();
}
