#include "./include/logger.h"
#include "./include/mprpcapplication.h"
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <thread>

namespace {
std::string LoadLogMode() {
  std::string mode = MprpcApplication::GetConfig().Load("MPRPC_LOG_MODE");
  if (mode.empty()) {
    mode = "stdout";
  }
  return mode;
}

std::string LoadLogDir() {
  std::string logDir = MprpcApplication::GetConfig().Load("MPRPC_LOG_DIR");
  if (logDir.empty()) {
    logDir = "logs";
  }
  return logDir;
}

std::string LoadLogName() {
  std::string logName = MprpcApplication::GetConfig().Load("MPRPC_LOG_NAME");
  if (logName.empty()) {
    logName = "mprpc";
  }
  return logName;
}

// 队列容量上限，配置键 MPRPC_LOG_QUEUE_CAP（默认 10000，0 表示不设上限）。
std::size_t LoadQueueCapacity() {
  const std::string raw =
      MprpcApplication::GetConfig().Load("MPRPC_LOG_QUEUE_CAP");
  if (raw.empty()) {
    return 10000;
  }
  char *end = nullptr;
  // string to unsigned long,把字符串按**十进制(那个 10)**解析成无符号整数。
  // &end:这是个出参。解析完后,end 会指向"它解析不动了的那个字符"。
  const unsigned long value = std::strtoul(raw.c_str(), &end, 10);
  // 如果 raw 是一个合法的数字字符串，end 应该指向字符串末尾的 '\0'，即 end == raw.c_str() + raw.size()。
  // 如果 raw 不是一个合法的数字字符串，end 将指向 raw.c_str()，即解析失败；或者指向第一个非数字字符的位置。
  if (end == raw.c_str() || *end != '\0') {
    return 10000; // 非法配置，退回默认
  }
  return static_cast<std::size_t>(value);
}
} // namespace

RpcLogger::RpcLogger()
    : m_lockQueue(LoadQueueCapacity()), m_shutdown(false),
      m_mode(LoadLogMode()), m_dir(LoadLogDir()), m_name(LoadLogName()),
      m_configEpoch(1) {
  // 构造发生在 MprpcApplication::Init() 之前（单例懒构造），此处只是
  // 用 env/默认值做一个“引导(bootstrap)”配置，保证 Init 之前的少量日志也有去处；
  // 真正的配置在 Init 之后由 Configure() 锁定。
  StartConsumer();
}

RpcLogger::~RpcLogger() { Shutdown(); }

void RpcLogger::Configure() {
  if (IsShutdown()) {
    return;
  }

  // 在 MprpcApplication::Init() 之后调用：此刻 env/默认/配置文件都已就绪。
  // 一次性把配置 latch 进来，之后消费线程只读这些锁定值。
  const std::string mode = LoadLogMode();
  const std::string dir = LoadLogDir();
  const std::string name = LoadLogName();
  const std::size_t cap = LoadQueueCapacity();
  {
    std::lock_guard<std::mutex> lock(m_configMutex);
    m_mode = mode;
    m_dir = dir;
    m_name = name;
  }
  m_lockQueue.SetCapacity(cap);
  // 自增版本号（release）：消费线程下一行用 acquire 读到变更后重读一次配置。
  m_configEpoch.fetch_add(1, std::memory_order_release);
}

void RpcLogger::StartConsumer() {
  m_consumer = std::thread([this]() {
    FILE *pf = nullptr;
    std::string currentFilePath;

    // 消费线程本地缓存的配置 + 已知的配置版本号。localEpoch 初值故意与构造时
    // 的 m_configEpoch(1) 不同，保证首轮必定从 latch 中加载一次。
    std::string mode;
    std::string dir;
    std::string name;
    unsigned long localEpoch = 0;

    // 仅当 Configure() 改过配置（epoch 变化）时，才加锁重读一次；
    // 否则每行只是一次原子读 + 比较，几乎零开销。
    auto refreshConfig = [&]() {
      const unsigned long epoch = m_configEpoch.load(std::memory_order_acquire);
      if (epoch != localEpoch) {
        std::lock_guard<std::mutex> lock(m_configMutex);
        mode = m_mode;
        dir = m_dir;
        name = m_name;
        localEpoch = epoch;
      }
    };

    // 把“写出一行”的逻辑收拢在消费线程内，pf / currentFilePath 也只归它所有。
    // fopen 失败时降级（A2：跳过本行文件写入，绝不退出进程）。
    auto writeLine = [&](const std::string &line) {
      if (mode == "stdout") {
        std::cout << line << std::flush;
        return;
      }

      time_t now = time(nullptr);
      tm nowtm = {};
      if (!RpcLogger::LocalTime(now, nowtm)) {
        std::cerr << line;
        return;
      }

      mkdir(dir.c_str(), 0755);

      char desiredPath[512] = {0};
      const int written = snprintf(desiredPath, sizeof(desiredPath),
                                   "%s/%d-%02d-%02d-%s.log", dir.c_str(),
                                   nowtm.tm_year + 1900, nowtm.tm_mon + 1,
                                   nowtm.tm_mday, name.c_str());
      if (written < 0 || static_cast<std::size_t>(written) >= sizeof(desiredPath)) {
        std::cerr << "log file path too long, skip file write" << std::endl;
        return;
      }

      if (pf == nullptr || currentFilePath != desiredPath) {
        if (pf != nullptr) {
          fflush(pf);
          fclose(pf);
          pf = nullptr;
        }

        pf = fopen(desiredPath, "a+");
        if (pf == nullptr) {
          std::cerr << "open log file error: " << desiredPath << std::endl;
          currentFilePath.clear();
          return; // 降级：丢弃本行的文件写入，不退出进程
        }

        currentFilePath = desiredPath;
      }

      fputs(line.c_str(), pf);
      fflush(pf);
    };

    // 消费循环：Pop 返回 false 表示“已关闭且排空”，此时退出。
    std::size_t reportedDropped = 0;
    std::string msg;
    while (m_lockQueue.Pop(msg)) {
      refreshConfig(); // 仅在 Configure() 改过配置时才真正重读

      // 队列满丢弃的可见化：把新增的丢弃数作为一条 [RPC] 日志写出，
      // 这样“丢了多少条”落在与正常日志相同的 sink 里。
      const std::size_t dropped = m_lockQueue.DroppedCount();
      if (dropped > reportedDropped) {
        char note[160] = {0};
        snprintf(note, sizeof(note),
                 "[RPC][WARN] dropped %zu log message(s) due to full queue\n",
                 dropped - reportedDropped);
        writeLine(note);
        reportedDropped = dropped;
      }
      writeLine(msg);
    }

    // 退出前收尾：flush 并关闭文件，保证不丢尾巴。
    if (pf != nullptr) {
      fflush(pf);
      fclose(pf);
      pf = nullptr;
    }
  });
}

RpcLogger &RpcLogger::GetInstance() {
  // 故意泄漏单例：进程退出时不跑它的析构，从而不依赖静态析构顺序
  // （消费线程会用到其它静态对象，如全局配置 MprpcApplication::m_config）。
  // 真正的清理由 main() 显式调用 Shutdown() 完成。
  static RpcLogger *logger = new RpcLogger();
  return *logger;
}

void RpcLogger::Shutdown() {
  std::lock_guard<std::mutex> lock(m_lifecycleMutex);
  if (m_shutdown) {
    return; // 幂等：重复调用安全
  }
  m_shutdown = true;
  m_lockQueue.Stop(); // 唤醒消费者：排空后 Pop 返回 false
  if (m_consumer.joinable()) {
    m_consumer.join(); // 等消费者把队列排空、flush、fclose
  }
}

bool RpcLogger::IsShutdown() const {
  std::lock_guard<std::mutex> lock(m_lifecycleMutex);
  return m_shutdown;
}

bool RpcLogger::LocalTime(time_t now, tm &out) {
  return localtime_r(&now, &out) != nullptr;
}

std::size_t RpcLogger::DroppedCount() const {
  return m_lockQueue.DroppedCount();
}

std::string RpcLogger::BuildPrefix(RpcLogLevel level) const {
  time_t now = time(nullptr);
  tm nowtm = {};
  char buf[128] = {0};
  if (LocalTime(now, nowtm)) {
    snprintf(buf, sizeof(buf), "[RPC][%s] %02d:%02d:%02d => ",
             (level == RpcLogLevel::INFO ? "INFO" : "ERROR"), nowtm.tm_hour,
             nowtm.tm_min, nowtm.tm_sec);
  } else {
    snprintf(buf, sizeof(buf), "[RPC][%s] => ",
             (level == RpcLogLevel::INFO ? "INFO" : "ERROR"));
  }
  return buf;
}

void RpcLogger::Log(std::string msg, RpcLogLevel level) {
  if (IsShutdown()) {
    return;
  }
  msg.insert(0, BuildPrefix(level));
  msg += '\n';
  m_lockQueue.Push(msg);
}
