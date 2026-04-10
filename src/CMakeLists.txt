# 框架核心源文件列表
set(MPRPC_SOURCES
    mprpcapplication.cc
    mprpcconfig.cc
    mprpcchannel.cc
    mprpcprovider.cc
    mprpccontroller.cc
    zookeeperutil.cc
    logger.cc
    rpcherder.pb.cc     # protoc 自动生成的 RPC 帧头序列化代码
)

# 将框架编译为静态库 mprpc
add_library(mprpc ${MPRPC_SOURCES})

# 链接 mprpc 所需的第三方库
target_link_libraries(mprpc
    muduo_net       # Muduo 网络库（TcpServer / EventLoop）
    muduo_base      # Muduo 基础库（日志、线程）
    protobuf        # Protocol Buffers 序列化
    zookeeper_mt    # ZooKeeper 多线程 C 客户端
    pthread         # POSIX 线程
)
