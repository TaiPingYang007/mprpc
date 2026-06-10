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
add_library(mprpc STATIC ${MPRPC_SOURCES})

# 框架自身代码开启 -Wall -Wextra；用 PRIVATE 避免把告警标志传染给下游使用者。
# 生成代码（rpcherder.pb.cc）的告警不强求，这里只约束我们手写的源文件。
target_compile_options(mprpc
    PRIVATE
        -Wall
        -Wextra
)

target_include_directories(mprpc
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src/include
)

set_target_properties(mprpc PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib
)

# 链接 mprpc 所需的第三方库
target_link_libraries(mprpc
    PUBLIC
        muduo_net       # Public headers expose Muduo types
        muduo_base      # Muduo 基础库（日志、线程）
        protobuf        # Public headers expose Protocol Buffers types
        zookeeper_mt    # Public headers expose ZooKeeper types
        pthread         # Required by the static RPC library and dependencies
)

install(TARGETS mprpc
    ARCHIVE DESTINATION lib
)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/src/include/
    DESTINATION include
)
