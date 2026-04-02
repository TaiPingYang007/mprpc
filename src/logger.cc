#include "./include/logger.h"
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <thread>

// 构造函数
Logger::Logger() {
  // 启动专门的写日志线程
  std::thread consumer([&]() {
    FILE *pf = nullptr;   // 保持文件一直打开的指针
    int current_day = -1; // 记录当前打开的文件是哪一天的

    while (true) {
      // 1. 从队列拿数据（如果队列为空，线程会在这里深度睡眠，极其省 CPU）
      std::string msg = m_lockQueue.Pop();

      // 2. 获取当前的时间
      time_t now = time(nullptr);
      tm *nowtm = localtime(&now);

      // 3. 极其经典的“零点切割”逻辑！
      // 如果文件还没打开，或者发现“现在的天数”和“记录的天数”不一样了（跨越了凌晨12点）
      if (pf == nullptr || nowtm->tm_mday != current_day) {

        // 把昨天的老文件关掉
        if (pf != nullptr) {
          fclose(pf);
        }

        // 组装新一天的文件名并打开
        char file_name[128];
        sprintf(file_name, "%d-%d-%d-log.txt", nowtm->tm_year + 1900,
                nowtm->tm_mon + 1, nowtm->tm_mday);
        pf = fopen(file_name, "a+");
        if (pf == nullptr) {
          std::cout << "open log file error!" << std::endl;
          exit(EXIT_FAILURE);
        }

        // 更新记录的天数
        current_day = nowtm->tm_mday;
      }

      // 4. 将日志内容写入文件
      fputs(msg.c_str(), pf);

      // 5. 极其关键的一步：刷盘！
      // 因为我们没有立刻 fclose，C标准库会把日志攒在内存里。
      // fflush 能强制把这一行日志立刻写入操作系统的磁盘里，这样你在线上 tail -f
      // 实时看日志时才能立刻看到！
      fflush(pf);
    }
  });

  // 设置分离线程
  consumer.detach();
}

// 单列模式
Logger &Logger::GetInstance() {
  static Logger logger;
  return logger;
}

// 设置日志的级别
void Logger::SetLogLevel(LogLevel level) {
  // INFO 为 0, ERROR 为 1
  m_loglevel = static_cast<int>(level);
}

// 写日志，把日志信息写入lockqueue缓冲区中
void Logger::Log(std::string msg, int level) {
  // 1. 获取当前精确时间（事情发生的瞬间）
  time_t now = time(nullptr);
  tm *nowtm = localtime(&now);

  // 2. 准备格式化缓冲区
  char buf[128] = {0};
  // 使用 sprintf 一次性拼出前缀：[级别] 时:分:秒 =>
  // %02d 表示如果数字只有一位，前面自动补0（比如 09:05:01，更整齐）
  sprintf(buf, "[%s] %02d:%02d:%02d => ", (level == 0 ? "INFO" : "ERROR"),
          nowtm->tm_hour, nowtm->tm_min, nowtm->tm_sec);

  // 3. 把前缀插入到日志内容的最前面
  msg.insert(0, buf);

  msg += '\n';
  // 4. 稳稳地推入队列
  m_lockQueue.Push(msg);
}