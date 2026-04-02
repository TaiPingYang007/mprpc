set(SRC_LIST
    ${PROJECT_SOURCE_DIR}/example/caller/callfriendservice.cc
    ${PROJECT_SOURCE_DIR}/example/friend.pb.cc
)

add_executable(consumer ${SRC_LIST})
target_link_libraries(consumer mprpc mymuduo protobuf pthread)