set(SRC_LIST
    ${PROJECT_SOURCE_DIR}/example/callee/firendservice.cc
    ${PROJECT_SOURCE_DIR}/example/friend.pb.cc
)

add_executable(provider ${SRC_LIST})
target_link_libraries(provider mprpc mymuduo protobuf pthread)