#pragma once
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>

// 异步写日志的日志队列 ps:使用了模板就不能使用分文件编写
//
// 有界 + 可关闭：
//   - 满时丢弃“最旧”一条并计数：生产者永不阻塞，适合日志这种绝不该拖慢
//     业务/RPC 热路径的场景；丢弃数可被观测，反映“丢了多少条”。
//   - Stop() 置停止标志并唤醒消费者；消费者把剩余消息排空后 Pop 返回
//     false，从而优雅退出（同构 ThreadPool::shutdown 的 stop + drain）。
template <class T> class RpcLockQueue {
public:
  // capacity 为队列容量上限；0 表示不设上限（退化为无界）。
  explicit RpcLockQueue(std::size_t capacity = 10000)
      : m_capacity(capacity), m_dropped(0), m_stop(false) {}

  // 生产者（多个线程向队列写东西）：满则丢最旧 + 计数，永不阻塞。
  void Push(const T &msg) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_stop) {
      // 已经关闭，不再接收，避免没有消费者时仍无限堆积。
      return;
    }
    if (m_capacity != 0 && m_queue.size() >= m_capacity) {
      m_queue.pop(); // 丢弃最旧的一条
      ++m_dropped;
    }
    m_queue.push(msg);
    m_condvar.notify_one();
  }

  // 消费者（一个线程向队列拿东西）。
  // 返回 true 并通过 out 带出一条消息；
  // 仅当“已关闭且队列已排空”时返回 false，作为消费者的退出信号。
  bool Pop(T &out) {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_condvar.wait(lock, [this] { return m_stop || !m_queue.empty(); });
    if (m_queue.empty()) {
      // 能醒来又为空，必然是 m_stop == true：队列已排空，通知消费者退出。
      return false;
    }
    out = m_queue.front();
    m_queue.pop();
    return true;
  }

  // 关闭队列：置停止标志并唤醒消费者，使其排空剩余消息后退出。
  void Stop() {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_stop = true;
    }
    m_condvar.notify_all();
  }

  // 运行期调整容量上限（供配置 latch 使用）。若当前积压超过新上限，
  // 按“丢最旧 + 计数”把超出部分清掉，保证立即满足新的内存界限。
  void SetCapacity(std::size_t capacity) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_capacity = capacity;
    while (m_capacity != 0 && m_queue.size() > m_capacity) {
      m_queue.pop();
      ++m_dropped;
    }
  }

  // 累计因队列满而被丢弃的消息条数（供观测/测试使用）。
  std::size_t DroppedCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_dropped;
  }

private:
  std::queue<T> m_queue;
  mutable std::mutex m_mutex;
  std::condition_variable m_condvar;
  std::size_t m_capacity;
  std::size_t m_dropped;
  bool m_stop;
};
