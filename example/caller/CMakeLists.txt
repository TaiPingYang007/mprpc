add_executable(
  consumer
  ${PROJECT_SOURCE_DIR}/example/caller/callfriendservice.cc
  ${PROJECT_SOURCE_DIR}/example/friend.pb.cc)

target_include_directories(consumer PRIVATE ${PROJECT_SOURCE_DIR}/example)
target_link_libraries(consumer PRIVATE mprpc)

add_executable(
  user_consumer
  ${PROJECT_SOURCE_DIR}/example/caller/calluserservice.cc
  ${PROJECT_SOURCE_DIR}/example/user.pb.cc)

target_include_directories(user_consumer PRIVATE ${PROJECT_SOURCE_DIR}/example)
target_link_libraries(user_consumer PRIVATE mprpc)
