#pragma once
#include "lockqueue.h"
#include <cstdio>
#include <string>

enum class LogLevel {
  INFO,
  ERROR,
};

class Logger {
public:
  static Logger &GetInstance();

  void SetLogLevel(LogLevel level);
  void Log(std::string msg, int level);

private:
  Logger();
  void StartConsumer();
  std::string BuildPrefix(int level) const;

  int m_loglevel;
  LockQueue<std::string> m_lockQueue;

  Logger(const Logger &) = delete;
  Logger(Logger &&) = delete;
};

#define LOG_INFO(logmsgformat, ...)                                            \
  do {                                                                         \
    Logger &logger = Logger::GetInstance();                                    \
    logger.SetLogLevel(LogLevel::INFO);                                        \
    char c[1024] = {0};                                                        \
    snprintf(c, 1024, logmsgformat, ##__VA_ARGS__);                            \
    logger.Log(c, 0);                                                          \
  } while (0)

#define LOG_ERROR(logmsgformat, ...)                                           \
  do {                                                                         \
    Logger &logger = Logger::GetInstance();                                    \
    logger.SetLogLevel(LogLevel::ERROR);                                       \
    char c[1024] = {0};                                                        \
    snprintf(c, 1024, logmsgformat, ##__VA_ARGS__);                            \
    logger.Log(c, 1);                                                          \
  } while (0)
