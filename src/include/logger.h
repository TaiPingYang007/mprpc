#pragma once
#include "lockqueue.h"
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

enum class RpcLogLevel {
  INFO,
  ERROR,
};

class RpcLogger {
public:
  static RpcLogger &GetInstance();

  void SetLogLevel(RpcLogLevel level);
  void Log(std::string msg, int level);

  // 从当前 MprpcConfig 一次性锁定（latch）日志配置：输出模式 / 目录 / 队列容量。
  // 应在 MprpcApplication::Init() 之后调用（Init 内部已自动调用一次）。
  // latch 之后，消费线程不再每行读配置——时序确定、热路径零配置开销。
  // 可重复调用，最后一次生效。
  void Configure();

  // 显式关闭：唤醒消费线程，排空队列、flush、fclose，并 join 消费线程。
  // 幂等；生命周期由 main() 控制，不依赖静态析构顺序。
  void Shutdown();

  // 累计被丢弃的日志条数（队列满时丢最旧），主要供测试/观测使用。
  std::size_t DroppedCount() const;

private:
  RpcLogger();
  ~RpcLogger();
  void StartConsumer();
  std::string BuildPrefix(int level) const;

  int m_loglevel;
  RpcLockQueue<std::string> m_lockQueue;
  std::thread m_consumer;
  bool m_shutdown;

  // —— 已 latch 的日志配置：Configure() 写、消费线程读 ——
  // m_configEpoch 每次 Configure() 自增；消费线程据此判断“配置变了，需重读”，
  // 从而把每行的配置开销降为一次原子读，只有真正变更那一刻才加锁重读。
  mutable std::mutex m_configMutex;
  std::string m_mode; // 受 m_configMutex 保护
  std::string m_dir;  // 受 m_configMutex 保护
  std::atomic<unsigned long> m_configEpoch;

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
