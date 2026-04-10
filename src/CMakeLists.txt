set(MPRPC_LIBRARY_SOURCES
    logger.cc
    mprpcapplication.cc
    mprpcchannel.cc
    mprpcconfig.cc
    mprpccontroller.cc
    mprpcprovider.cc
    rpcherder.pb.cc
    zookeeperutil.cc)

add_library(mprpc STATIC ${MPRPC_LIBRARY_SOURCES})
add_library(mprpc::mprpc ALIAS mprpc)

target_include_directories(
  mprpc
  PUBLIC
    ${PROJECT_SOURCE_DIR}/src/include
    ${MUDUO_INCLUDE_DIR}
    ${Protobuf_INCLUDE_DIRS})

target_link_libraries(
  mprpc
  PUBLIC ${MPRPC_PROTOBUF_TARGET} ${MUDUO_NET_LIBRARY} ${MUDUO_BASE_LIBRARY}
         zookeeper_mt Threads::Threads)
