# --- 客户端示例：调用 User 服务 ---
add_executable(calluserservice
    calluserservice.cc
    ${PROJECT_SOURCE_DIR}/example/user.pb.cc
)
set_target_properties(calluserservice PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin
)
target_link_libraries(calluserservice mprpc)

# --- 客户端示例：调用 Friend 服务 ---
add_executable(callfriendservice
    callfriendservice.cc
    ${PROJECT_SOURCE_DIR}/example/friend.pb.cc
)
set_target_properties(callfriendservice PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin
)
target_link_libraries(callfriendservice mprpc)
