// A1 验收：连续写 N 条日志后立即 Shutdown()，文件里必须有完整 N 条，不丢尾巴。
//
// 通过环境变量驱动配置（Load() 优先读 env），无需配置文件或 MprpcApplication::Init：
//   MPRPC_LOG_MODE=file，MPRPC_LOG_DIR=<本测试专用目录>。
#include "logger.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>

namespace {
std::string TodayLogPath(const std::string &dir) {
  time_t now = time(nullptr);
  tm *t = localtime(&now);
  char name[256] = {0};
  snprintf(name, sizeof(name), "%s/%d-%02d-%02d-log.txt", dir.c_str(),
           t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
  return name;
}
} // namespace

int main() {
  const std::string dir = "test_logs_shutdown";
  setenv("MPRPC_LOG_MODE", "file", 1);
  setenv("MPRPC_LOG_DIR", dir.c_str(), 1);

  // D 模式：配置就绪后显式 latch 一次（真实程序里由 MprpcApplication::Init 代劳）。
  RpcLogger::GetInstance().Configure();

  // 清掉上一次运行残留（logger 以 "a+" 追加打开，否则行数会累加）。
  std::remove(TodayLogPath(dir).c_str());

  const int kLines = 5000; // < 默认容量 10000，确保本场景零丢弃
  for (int i = 0; i < kLines; ++i) {
    RPC_LOG_INFO("acceptance-line-%d", i);
  }

  // 关键：立即 Shutdown，验证它会把队列排空后再 flush/fclose/join。
  RpcLogger::GetInstance().Shutdown();

  std::ifstream in(TodayLogPath(dir).c_str());
  if (!in) {
    std::cerr << "FAIL A1: log file not found: " << TodayLogPath(dir)
              << std::endl;
    return 1;
  }

  int lines = 0;
  std::string line;
  while (std::getline(in, line)) {
    ++lines;
  }

  if (lines != kLines) {
    std::cerr << "FAIL A1: expected " << kLines << " lines, got " << lines
              << " (tail loss!)" << std::endl;
    return 1;
  }

  std::cout << "PASS A1: wrote " << kLines << " lines, Shutdown drained all "
            << lines << " with no tail loss" << std::endl;
  return 0;
}
