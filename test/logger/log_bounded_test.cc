// B1 验收：小容量队列被狂打日志压满时，内存不会无限上涨——
// 表现为队列大小被 cap 住、超出的最旧日志被丢弃并计数（DroppedCount > 0）。
//
// 用一个很小的容量（MPRPC_LOG_QUEUE_CAP=16）让丢弃可被确定性地触发：
// 生产者远快于“每行 flush”的文件消费者，队列必然被压满。
#include "logger.h"

#include <cstddef>
#include <cstdlib>
#include <iostream>

int main() {
  setenv("MPRPC_LOG_QUEUE_CAP", "16", 1);
  setenv("MPRPC_LOG_MODE", "file", 1);
  setenv("MPRPC_LOG_DIR", "test_logs_bounded", 1);
  setenv("MPRPC_LOG_NAME", "bounded-rpc", 1);

  // D 模式：显式 latch 配置，把队列容量锁定为 16（SetCapacity）。
  RpcLogger::GetInstance().Configure();

  const int kFlood = 200000;
  for (int i = 0; i < kFlood; ++i) {
    RPC_LOG_INFO("flood-line-%d", i);
  }

  // 此刻生产者已远远跑在消费者前面，cap=16 必然触发大量丢弃。
  const std::size_t dropped = RpcLogger::GetInstance().DroppedCount();
  RpcLogger::GetInstance().Shutdown();

  if (dropped == 0) {
    std::cerr << "FAIL B1: flooded " << kFlood
              << " messages through a 16-slot queue but dropped 0 "
                 "(queue grew unbounded?)"
              << std::endl;
    return 1;
  }

  std::cout << "PASS B1: flooded " << kFlood << " messages, dropped " << dropped
            << "; queue stayed bounded at cap=16" << std::endl;
  return 0;
}
