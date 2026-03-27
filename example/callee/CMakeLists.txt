set (SRC_LIST
    ${PROJECT_SOURCE_DIR}/example/callee/userservice.cc
    ${PROJECT_SOURCE_DIR}/example/user.pb.cc
)

add_executable(provider ${SRC_LIST}) 
target_link_libraries(provider mprpc protobuf)