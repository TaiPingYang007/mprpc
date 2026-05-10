#pragma once

#ifndef THREADED
#define THREADED
#endif

#include <semaphore.h>
#include <string>
#include <vector>
#include <zookeeper/zookeeper.h>

class ZkClient {
public:
  ZkClient();
  ~ZkClient();

  bool Start(bool retryUntilReady = false);
  bool Create(const char *path, const char *data, int datalen, int state = 0);
  std::string CreateSequential(const char *pathPrefix, const char *data,
                               int datalen, int state = 0);
  std::string GetData(const char *path);
  std::vector<std::string> GetChildren(const char *path);

private:
  zhandle_t *m_zhandle;
};
