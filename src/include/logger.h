#pragma once
#include "lockqueue.h"

// Mprpc框架提供的日志系统
enum class LogLevel {
  INFO,  // 普通消息
  ERROR, // 错误消息
};

class Logger {
public:
  // 单列模式
  static Logger &GetInstance();

  // 设置日志的级别
  void SetLogLevel(LogLevel level);

  // 写日志
  void Log(std::string msg, int level);

private:
  int m_loglevel;                     // 记录日志级别
  LockQueue<std::string> m_lockQueue; // 日志缓冲队列

  Logger();
  Logger(const Logger &) = delete;
  Logger(Logger &&) = delete;
};

// 定义宏：普通信息日志
#define LOG_INFO(logmsgformat, ...)                                            \
  do {                                                                         \
    Logger &logger = Logger::GetInstance();                                    \
    logger.SetLogLevel(LogLevel::INFO);                                        \
    char c[1024] = {0};                                                        \
    snprintf(c, 1024, logmsgformat, ##__VA_ARGS__);                            \
    logger.Log(c, 0);                                                          \
  } while (0)

// 定义宏：错误信息日志
#define LOG_ERROR(logmsgformat, ...)                                           \
  do {                                                                         \
    Logger &logger = Logger::GetInstance();                                    \
    logger.SetLogLevel(LogLevel::ERROR);                                       \
    char c[1024] = {0};                                                        \
    snprintf(c, 1024, logmsgformat, ##__VA_ARGS__);                            \
    logger.Log(c, 1);                                                          \
  } while (0)
