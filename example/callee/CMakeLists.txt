set (SRC_LIST
    ${PROJECT_SOURCE_DIR}/example/callee/callee.cpp
    ${PROJECT_SOURCE_DIR}/example/user.pb.cc
)

add_executable(provider ${SRC_LIST}) 