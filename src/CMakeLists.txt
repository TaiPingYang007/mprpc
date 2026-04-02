set(MPRPC_LIBRARY_SOURCES
    logger.cc
    mprpcapplication.cc
    mprpcchannel.cc
    mprpcconfig.cc
    mprpccontroller.cc
    mprpcprovider.cc
    rpcherder.pb.cc
    zookeeperutil.cc)

add_library(mprpc SHARED ${MPRPC_LIBRARY_SOURCES})
add_library(mprpc::mprpc ALIAS mprpc)

target_include_directories(
  mprpc
  PUBLIC
    ${PROJECT_SOURCE_DIR}/src/include
    ${Protobuf_INCLUDE_DIRS})

target_link_directories(mprpc PRIVATE ${PROJECT_SOURCE_DIR}/lib)

target_link_libraries(
  mprpc
  PUBLIC ${MPRPC_PROTOBUF_TARGET} mymuduo
  PRIVATE zookeeper_mt Threads::Threads)
