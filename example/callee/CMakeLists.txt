add_executable(
  provider
  ${PROJECT_SOURCE_DIR}/example/callee/firendservice.cc
  ${PROJECT_SOURCE_DIR}/example/friend.pb.cc)

target_include_directories(provider PRIVATE ${PROJECT_SOURCE_DIR}/example)
target_link_libraries(provider PRIVATE mprpc)

add_executable(
  user_provider
  ${PROJECT_SOURCE_DIR}/example/callee/userservice.cc
  ${PROJECT_SOURCE_DIR}/example/user.pb.cc)

target_include_directories(user_provider PRIVATE ${PROJECT_SOURCE_DIR}/example)
target_link_libraries(user_provider PRIVATE mprpc)
