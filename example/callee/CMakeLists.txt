# --- 服务端示例：User 服务 ---
# 需要同时编译 proto 生成的序列化代码 user.pb.cc
add_executable(userservice
    userservice.cc
    ${PROJECT_SOURCE_DIR}/example/user.pb.cc
)
set_target_properties(userservice PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin
)
# 链接框架静态库（已包含 protobuf / muduo / zookeeper 等传递依赖）
target_link_libraries(userservice mprpc)

# --- 服务端示例：Friend 服务 ---
add_executable(friendservice
    firendservice.cc
    ${PROJECT_SOURCE_DIR}/example/friend.pb.cc
)
set_target_properties(friendservice PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin
)
target_link_libraries(friendservice mprpc)
