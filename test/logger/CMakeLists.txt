# RpcLogger 验收测试：A1（shutdown 不丢尾巴）+ B1（有界队列丢弃计数）。
# 链接 mprpc 静态库即可拿到 logger.h（mprpc 的 PUBLIC include 暴露 src/include）。

add_executable(log_shutdown_test log_shutdown_test.cc)
target_link_libraries(log_shutdown_test PRIVATE mprpc)

add_executable(log_bounded_test log_bounded_test.cc)
target_link_libraries(log_bounded_test PRIVATE mprpc)
