#include "zookeeperutil.h"
#include "logger.h"
#include "mprpcapplication.h"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <semaphore.h>
#include <string>
#include <thread>
#include <time.h>
#include <vector>
#include <zookeeper/zookeeper.h>

namespace {
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

std::string NormalizeNamespace() {
  std::string ns = MprpcApplication::GetConfig().Load("MPRPC_ZK_NAMESPACE");
  if (ns.empty()) {
    ns = "mprpc";
  }

  while (!ns.empty() && ns.front() == '/') {
    ns.erase(ns.begin());
  }
  while (!ns.empty() && ns.back() == '/') {
    ns.pop_back();
  }
  if (ns.empty()) {
    ns = "mprpc";
  }
  return ns;
}

timespec BuildAbsoluteTimeout(int timeoutMs) {
  timespec absTimeout;
  clock_gettime(CLOCK_REALTIME, &absTimeout);
  absTimeout.tv_sec += timeoutMs / 1000;
  absTimeout.tv_nsec += static_cast<long>(timeoutMs % 1000) * 1000000L;
  if (absTimeout.tv_nsec >= 1000000000L) {
    absTimeout.tv_sec += 1;
    absTimeout.tv_nsec -= 1000000000L;
  }
  return absTimeout;
}

} // namespace

void global_watcher(zhandle_t *zh, int type, int state, const char *path,
                    void *watcherCtx) {
  (void)path;
  (void)watcherCtx;
  if (type == ZOO_SESSION_EVENT && state == ZOO_CONNECTED_STATE) {
    sem_t *sem = const_cast<sem_t *>(
        static_cast<const sem_t *>(zoo_get_context(zh)));
    if (sem != nullptr) {
      sem_post(sem);
    }
  }
}

ZkClient::ZkClient() : m_zhandle(nullptr) {}

ZkClient::~ZkClient() {
  if (m_zhandle != nullptr) {
    zookeeper_close(m_zhandle);
    m_zhandle = nullptr;
  }
}

bool ZkClient::Start(bool retryUntilReady) {
  if (m_zhandle != nullptr) {
    return true;
  }

  const std::string endpoints = MprpcApplication::GetConfig().Load("ZK_ENDPOINTS");
  const int connectTimeoutMs = LoadTimeoutMs("ZK_CONNECT_TIMEOUT_MS", 5000);
  const int sessionTimeoutMs = LoadTimeoutMs("ZK_SESSION_TIMEOUT_MS", 30000);
  const int retryDelayMs = 1000;
  const int maxAttempts = retryUntilReady ? 30 : 1;

  for (int attempt = 0; attempt < maxAttempts; ++attempt) {
    m_zhandle = zookeeper_init(endpoints.c_str(), global_watcher, sessionTimeoutMs,
                               nullptr, nullptr, 0);
    if (m_zhandle == nullptr) {
      LOG_ERROR("zookeeper_init error! endpoints=%s", endpoints.c_str());
    } else {
      sem_t sem;
      sem_init(&sem, 0, 0);
      zoo_set_context(m_zhandle, &sem);

      const timespec absTimeout = BuildAbsoluteTimeout(connectTimeoutMs);
      const int waitResult = sem_timedwait(&sem, &absTimeout);
      zoo_set_context(m_zhandle, nullptr);
      sem_destroy(&sem);
      if (waitResult == 0) {
        LOG_INFO("zkclient start success! endpoints=%s", endpoints.c_str());
        return true;
      }

      LOG_ERROR("zookeeper connect timeout after %d ms, endpoints=%s, reason=%s",
                connectTimeoutMs, endpoints.c_str(), std::strerror(errno));
      zookeeper_close(m_zhandle);
      m_zhandle = nullptr;
    }

    if (!retryUntilReady || attempt + 1 >= maxAttempts) {
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
  }

  return false;
}

bool ZkClient::Create(const char *path, const char *data, int datalen, int state) {
  if (m_zhandle == nullptr) {
    LOG_ERROR("zookeeper client is not connected, create failed. path=%s", path);
    return false;
  }

  if (path == nullptr || path[0] == '\0') {
    return false;
  }

  if (state == 0 && zoo_exists(m_zhandle, path, 0, nullptr) == ZOK) {
    return true;
  }

  char createdPath[256] = {0};
  int bufferLen = static_cast<int>(sizeof(createdPath));
  const int flag = zoo_create(m_zhandle, path, data, datalen, &ZOO_OPEN_ACL_UNSAFE,
                              state, createdPath, bufferLen);
  if (flag == ZOK || flag == ZNODEEXISTS) {
    if (createdPath[0] != '\0') {
      LOG_INFO("znode create success, path=%s", createdPath);
    } else {
      LOG_INFO("znode create success, path=%s", path);
    }
    return true;
  }

  if (flag == ZNONODE && state == 0) {
    return false;
  }

  LOG_ERROR("znode create error, path=%s, flag=%d", path, flag);
  return false;
}

std::string ZkClient::CreateSequential(const char *pathPrefix, const char *data,
                                       int datalen, int state) {
  if (m_zhandle == nullptr) {
    LOG_ERROR("zookeeper client is not connected, create sequential failed. path=%s",
              pathPrefix);
    return "";
  }

  char createdPath[256] = {0};
  const int flag = zoo_create(m_zhandle, pathPrefix, data, datalen,
                              &ZOO_OPEN_ACL_UNSAFE, state | ZOO_SEQUENCE,
                              createdPath, static_cast<int>(sizeof(createdPath)));
  if (flag != ZOK) {
    LOG_ERROR("znode create sequential error, path=%s, flag=%d", pathPrefix,
              flag);
    return "";
  }

  LOG_INFO("znode create sequential success, path=%s", createdPath);
  return createdPath;
}

std::string ZkClient::GetData(const char *path) {
  if (m_zhandle == nullptr) {
    LOG_ERROR("zookeeper client is not connected, get data failed. path=%s", path);
    return "";
  }

  char buffer[256] = {0};
  int bufferLen = static_cast<int>(sizeof(buffer));
  int flag = zoo_get(m_zhandle, path, 0, buffer, &bufferLen, nullptr);
  if (flag != ZOK) {
    LOG_ERROR("znode get error, path=%s, flag=%d", path, flag);
    return "";
  }

  return std::string(buffer, bufferLen);
}

std::vector<std::string> ZkClient::GetChildren(const char *path) {
  std::vector<std::string> children;
  if (m_zhandle == nullptr) {
    LOG_ERROR("zookeeper client is not connected, get children failed. path=%s",
              path);
    return children;
  }

  String_vector childVector;
  int flag = zoo_get_children(m_zhandle, path, 0, &childVector);
  if (flag != ZOK) {
    return children;
  }

  for (int i = 0; i < childVector.count; ++i) {
    children.push_back(childVector.data[i]);
  }

  deallocate_String_vector(&childVector);
  return children;
}
