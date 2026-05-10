#include "./include/mprpcchannel.h"
#include "./include/logger.h"
#include "./include/mprpcapplication.h"
#include "rpcherder.pb.h"
#include "zookeeperutil.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <google/protobuf/descriptor.h>
#include <iostream>
#include <map>
#include <mutex>
#include <netdb.h>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace {
const size_t kReceiveBufferSize = 1024;

class SocketFdGuard {
public:
  explicit SocketFdGuard(int fd) : m_fd(fd) {}
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
  return action + " error! errno=" + std::to_string(errno) +
         " reason=" + std::string(strerror(errno));
}

void SetRpcFailure(google::protobuf::RpcController *controller,
                   const std::string &errorMessage) {
  LOG_ERROR("%s", errorMessage.c_str());
  controller->SetFailed(errorMessage);
}

int LoadTimeoutMs(const std::string &key, int defaultValue) {
  const std::string value = MprpcApplication::GetConfig().Load(key);
  if (value.empty()) {
    return defaultValue;
  }

  try {
    const int timeoutMs = std::stoi(value);
    return timeoutMs > 0 ? timeoutMs : defaultValue;
  } catch (...) {
    return defaultValue;
  }
}

bool SetSocketTimeout(int socketFd, int optionName, int timeoutMs) {
  timeval timeout;
  timeout.tv_sec = timeoutMs / 1000;
  timeout.tv_usec = (timeoutMs % 1000) * 1000;
  return setsockopt(socketFd, SOL_SOCKET, optionName, &timeout,
                    sizeof(timeout)) == 0;
}

bool RestoreSocketFlags(int socketFd, int originalFlags) {
  return fcntl(socketFd, F_SETFL, originalFlags) != -1;
}

bool ConnectWithTimeout(int socketFd, const struct sockaddr *serverAddress,
                        socklen_t addressLength, int timeoutMs) {
  const int originalFlags = fcntl(socketFd, F_GETFL, 0);
  if (originalFlags == -1) {
    return false;
  }

  if (fcntl(socketFd, F_SETFL, originalFlags | O_NONBLOCK) == -1) {
    return false;
  }

  if (connect(socketFd, serverAddress, addressLength) == 0) {
    return RestoreSocketFlags(socketFd, originalFlags);
  }

  if (errno != EINPROGRESS) {
    const int savedErrno = errno;
    RestoreSocketFlags(socketFd, originalFlags);
    errno = savedErrno;
    return false;
  }

  pollfd descriptor;
  descriptor.fd = socketFd;
  descriptor.events = POLLOUT;
  descriptor.revents = 0;

  int pollResult = 0;
  do {
    pollResult = poll(&descriptor, 1, timeoutMs);
  } while (pollResult == -1 && errno == EINTR);

  if (pollResult <= 0) {
    const int savedErrno = (pollResult == 0) ? ETIMEDOUT : errno;
    RestoreSocketFlags(socketFd, originalFlags);
    errno = savedErrno;
    return false;
  }

  int socketError = 0;
  socklen_t errorLength = sizeof(socketError);
  if (getsockopt(socketFd, SOL_SOCKET, SO_ERROR, &socketError, &errorLength) ==
      -1) {
    const int savedErrno = errno;
    RestoreSocketFlags(socketFd, originalFlags);
    errno = savedErrno;
    return false;
  }

  if (socketError != 0) {
    RestoreSocketFlags(socketFd, originalFlags);
    errno = socketError;
    return false;
  }

  return RestoreSocketFlags(socketFd, originalFlags);
}

bool SendAll(int socketFd, const std::string &data) {
  size_t totalBytesSent = 0;
  while (totalBytesSent < data.size()) {
    const ssize_t currentBytesSent =
        send(socketFd, data.data() + totalBytesSent,
             data.size() - totalBytesSent, 0);
    if (currentBytesSent < 0 && errno == EINTR) {
      continue;
    }

    if (currentBytesSent <= 0) {
      return false;
    }

    totalBytesSent += static_cast<size_t>(currentBytesSent);
  }

  return true;
}

bool ParseHostData(const std::string &hostData, std::string *host,
                   uint16_t *port) {
  const std::string::size_type delimiterPos = hostData.rfind(':');
  if (delimiterPos == std::string::npos) {
    return false;
  }

  *host = hostData.substr(0, delimiterPos);
  try {
    *port = static_cast<uint16_t>(
        std::stoi(hostData.substr(delimiterPos + 1)));
  } catch (...) {
    return false;
  }
  return true;
}

bool ResolveEndpoint(const std::string &host, uint16_t port,
                     struct sockaddr_storage *storage, socklen_t *length) {
  addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;

  addrinfo *result = nullptr;
  std::string portString = std::to_string(port);
  const int rc = getaddrinfo(host.c_str(), portString.c_str(), &hints, &result);
  if (rc != 0 || result == nullptr) {
    return false;
  }

  std::memset(storage, 0, sizeof(*storage));
  std::memcpy(storage, result->ai_addr, result->ai_addrlen);
  *length = static_cast<socklen_t>(result->ai_addrlen);
  freeaddrinfo(result);
  return true;
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

std::string SelectProviderHostData(ZkClient *client, const std::string &serviceName,
                                   const std::string &methodName) {
  const std::string providerPath = BuildProviderPath(serviceName, methodName);
  std::vector<std::string> children = client->GetChildren(providerPath.c_str());
  if (children.empty()) {
    const std::string legacyPath = BuildMethodPath(serviceName, methodName);
    const std::string legacyData = client->GetData(legacyPath.c_str());
    if (!legacyData.empty()) {
      return legacyData;
    }

    const std::string rootLegacyPath = "/" + serviceName + "/" + methodName;
    return client->GetData(rootLegacyPath.c_str());
  }

  std::sort(children.begin(), children.end());
  static std::mutex rrMutex;
  static std::map<std::string, size_t> rrIndexMap;
  const std::string rrKey = serviceName + "/" + methodName;
  size_t index = 0;
  {
    std::lock_guard<std::mutex> lock(rrMutex);
    index = rrIndexMap[rrKey] % children.size();
    rrIndexMap[rrKey] = (rrIndexMap[rrKey] + 1) % children.size();
  }

  const std::string childPath = providerPath + "/" + children[index];
  return client->GetData(childPath.c_str());
}
} // namespace

void MprpcChannel::CallMethod(const google::protobuf::MethodDescriptor *method,
                              google::protobuf::RpcController *controller,
                              const google::protobuf::Message *request,
                              google::protobuf::Message *response,
                              google::protobuf::Closure *done) {
  (void)done;
  assert(controller != nullptr && "Error: RpcController cannot be nullptr!");
  if (controller == nullptr) {
    return;
  }

  const google::protobuf::ServiceDescriptor *serviceDesc = method->service();
  const std::string serviceName = serviceDesc->name();
  const std::string methodName = method->name();

  std::string argsString;
  if (!request->SerializeToString(&argsString)) {
    SetRpcFailure(controller, "serialize request error");
    return;
  }
  const uint32_t argsSize = static_cast<uint32_t>(argsString.size());

  mprpc::RpcHeader rpcHeader;
  rpcHeader.set_service_name(serviceName);
  rpcHeader.set_method_name(methodName);
  rpcHeader.set_args_size(argsSize);

  std::string rpcHeaderString;
  if (!rpcHeader.SerializeToString(&rpcHeaderString)) {
    SetRpcFailure(controller, "serialize rpc header error");
    return;
  }
  const uint32_t headerSize = static_cast<uint32_t>(rpcHeaderString.size());

  std::string sendRpcString;
  const uint32_t networkHeaderSize = htonl(headerSize);
  sendRpcString.insert(
      0, std::string(reinterpret_cast<const char *>(&networkHeaderSize), 4));
  sendRpcString += rpcHeaderString;
  sendRpcString += argsString;

  ZkClient zkclient;
  if (!zkclient.Start()) {
    SetRpcFailure(controller, "connect zookeeper timeout or failed");
    return;
  }

  const std::string hostData =
      SelectProviderHostData(&zkclient, serviceName, methodName);
  if (hostData.empty()) {
    SetRpcFailure(controller,
                  "get host data from zookeeper error! service=" + serviceName +
                      " method=" + methodName);
    return;
  }

  std::string host;
  uint16_t port = 0;
  if (!ParseHostData(hostData, &host, &port)) {
    SetRpcFailure(controller,
                  "invalid host data from zookeeper! data=" + hostData);
    return;
  }

  struct sockaddr_storage serverStorage;
  socklen_t serverLength = 0;
  if (!ResolveEndpoint(host, port, &serverStorage, &serverLength)) {
    SetRpcFailure(controller, "resolve target host error! host=" + host +
                                  " port=" + std::to_string(port));
    return;
  }

  const int clientfd = socket(serverStorage.ss_family, SOCK_STREAM, 0);
  if (clientfd == -1) {
    SetRpcFailure(controller, BuildSocketErrorMessage("socket create"));
    return;
  }
  SocketFdGuard guard(clientfd);

  const int connectTimeoutMs = LoadTimeoutMs("RPC_CONNECT_TIMEOUT_MS", 3000);
  const int sendTimeoutMs = LoadTimeoutMs("RPC_SEND_TIMEOUT_MS", 3000);
  const int recvTimeoutMs = LoadTimeoutMs("RPC_RECV_TIMEOUT_MS", 5000);

  if (!ConnectWithTimeout(guard.get(),
                          reinterpret_cast<struct sockaddr *>(&serverStorage),
                          serverLength, connectTimeoutMs)) {
    SetRpcFailure(controller, BuildSocketErrorMessage("connect"));
    return;
  }

  LOG_INFO("rpc connect success! service=%s method=%s target=%s:%u",
           serviceName.c_str(), methodName.c_str(), host.c_str(), port);

  if (!SetSocketTimeout(guard.get(), SO_SNDTIMEO, sendTimeoutMs)) {
    SetRpcFailure(controller, BuildSocketErrorMessage("set send timeout"));
    return;
  }

  if (!SetSocketTimeout(guard.get(), SO_RCVTIMEO, recvTimeoutMs)) {
    SetRpcFailure(controller, BuildSocketErrorMessage("set recv timeout"));
    return;
  }

  if (!SendAll(guard.get(), sendRpcString)) {
    SetRpcFailure(controller, BuildSocketErrorMessage("send"));
    return;
  }

  std::string responseString;
  char receiveBuffer[kReceiveBufferSize] = {0};
  while (true) {
    const ssize_t receivedBytes =
        recv(guard.get(), receiveBuffer, sizeof(receiveBuffer), 0);
    if (receivedBytes < 0 && errno == EINTR) {
      continue;
    }

    if (receivedBytes == 0) {
      break;
    }

    if (receivedBytes < 0) {
      SetRpcFailure(controller, BuildSocketErrorMessage("recv"));
      return;
    }

    responseString.append(receiveBuffer, static_cast<size_t>(receivedBytes));
  }

  if (responseString.empty()) {
    SetRpcFailure(controller, "recv empty response from rpc provider");
    return;
  }

  if (!response->ParseFromString(responseString)) {
    SetRpcFailure(controller, "parse error! response_str:" + responseString);
    return;
  }
}
