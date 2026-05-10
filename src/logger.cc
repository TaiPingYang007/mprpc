#include "./include/logger.h"
#include "./include/mprpcapplication.h"
#include <cstdlib>
#include <ctime>
#include <iostream>
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
} // namespace

Logger::Logger() : m_loglevel(0) { StartConsumer(); }

void Logger::StartConsumer() {
  std::thread consumer([this]() {
    FILE *pf = nullptr;
    int currentDay = -1;

    while (true) {
      const std::string msg = m_lockQueue.Pop();
      const std::string mode = LoadLogMode();
      if (mode == "stdout") {
        std::cout << msg << std::flush;
        continue;
      }

      time_t now = time(nullptr);
      tm *nowtm = localtime(&now);
      if (nowtm == nullptr) {
        std::cerr << msg;
        continue;
      }

      const std::string logDir = LoadLogDir();
      mkdir(logDir.c_str(), 0755);

      if (pf == nullptr || nowtm->tm_mday != currentDay) {
        if (pf != nullptr) {
          fclose(pf);
        }

        char fileName[128] = {0};
        snprintf(fileName, sizeof(fileName), "%s/%d-%02d-%02d-log.txt",
                 logDir.c_str(), nowtm->tm_year + 1900, nowtm->tm_mon + 1,
                 nowtm->tm_mday);
        pf = fopen(fileName, "a+");
        if (pf == nullptr) {
          std::cerr << "open log file error: " << fileName << std::endl;
          continue;
        }

        currentDay = nowtm->tm_mday;
      }

      fputs(msg.c_str(), pf);
      fflush(pf);
    }
  });

  consumer.detach();
}

Logger &Logger::GetInstance() {
  static Logger logger;
  return logger;
}

void Logger::SetLogLevel(LogLevel level) {
  m_loglevel = static_cast<int>(level);
}

std::string Logger::BuildPrefix(int level) const {
  time_t now = time(nullptr);
  tm *nowtm = localtime(&now);
  char buf[128] = {0};
  if (nowtm != nullptr) {
    snprintf(buf, sizeof(buf), "[%s] %02d:%02d:%02d => ",
             (level == 0 ? "INFO" : "ERROR"), nowtm->tm_hour,
             nowtm->tm_min, nowtm->tm_sec);
  } else {
    snprintf(buf, sizeof(buf), "[%s] => ", (level == 0 ? "INFO" : "ERROR"));
  }
  return buf;
}

void Logger::Log(std::string msg, int level) {
  msg.insert(0, BuildPrefix(level));
  msg += '\n';
  m_lockQueue.Push(msg);
}
