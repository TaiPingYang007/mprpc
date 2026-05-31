#pragma once
#include "lockqueue.h"
#include <cstdio>
#include <string>

enum class RpcLogLevel {
  INFO,
  ERROR,
};

class RpcLogger {
public:
  static RpcLogger &GetInstance();

  void SetLogLevel(RpcLogLevel level);
  void Log(std::string msg, int level);

private:
  RpcLogger();
  void StartConsumer();
  std::string BuildPrefix(int level) const;

  int m_loglevel;
  RpcLockQueue<std::string> m_lockQueue;

  RpcLogger(const RpcLogger &) = delete;
  RpcLogger(RpcLogger &&) = delete;
};

#define RPC_LOG_INFO(logmsgformat, ...)                                        \
  do {                                                                         \
    RpcLogger &logger = RpcLogger::GetInstance();                              \
    logger.SetLogLevel(RpcLogLevel::INFO);                                     \
    char c[1024] = {0};                                                        \
    snprintf(c, 1024, logmsgformat, ##__VA_ARGS__);                            \
    logger.Log(c, 0);                                                          \
  } while (0)

#define RPC_LOG_ERROR(logmsgformat, ...)                                       \
  do {                                                                         \
    RpcLogger &logger = RpcLogger::GetInstance();                              \
    logger.SetLogLevel(RpcLogLevel::ERROR);                                    \
    char c[1024] = {0};                                                        \
    snprintf(c, 1024, logmsgformat, ##__VA_ARGS__);                            \
    logger.Log(c, 1);                                                          \
  } while (0)
