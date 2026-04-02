#pragma once
#include <condition_variable>
#include <mutex>
#include <queue>

// 异步写日志的日志队列 ps:使用了模板就不能使用分文件编写
template <class T> class LockQueue {
public:
  // 生产者（多个线程向队列写东西）
  void Push(const T &msg) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push(msg);
    m_condvar.notify_one();
  }

  // 消费者（一个线程向队列拿东西）
  T Pop() {
    std::unique_lock<std::mutex> lock(m_mutex);
    while (m_queue.empty()) {
      // 日志队列为空，线程进入wait状态
      m_condvar.wait(lock);
    }
    T msg = m_queue.front();
    m_queue.pop();
    return msg;
  }

private:
  std::queue<T> m_queue;
  std::mutex m_mutex;
  std::condition_variable m_condvar;
};