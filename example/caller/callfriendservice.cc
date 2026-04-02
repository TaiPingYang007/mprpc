#include "../friend.pb.h"
#include "mprpcapplication.h"

int main(int argc, char **argv) {
  // rpc框架初始化
  MprpcApplication::Init(argc, argv);

  // 创建rpc通道
  fixbug::FriendServiceRpc_Stub stub(new MprpcChannel());

  // rpc方法调用
  fixbug::GetFriendListRequest request;
  request.set_userid(1);

  fixbug::GetFriendListResponse response;

  MprpcController controller;
  stub.GetFriendList(&controller, &request, &response, nullptr);

  // 先判断controller，判断这次调用是否成功
  if (controller.Failed()) {
    // 失败
    std::cout << "rpc GetFriendList error :" << controller.ErrorText()
              << std::endl;
  } else {
    // 调用成功，并不是业务逻辑成功
    if (response.result().errcode() == 0) {
      // 业务逻辑执行成功
      std::cout << "rpc GetFriendList success :" << std::endl;
      for (int i = 0; i < response.friend_list_size(); i++) {
        std::cout << "userid:" << response.friend_list(i).userid() << " "
                  << "name:" << response.friend_list(i).name() << std::endl;
      }
    } else {
      std::cout << "rpc GetFriendList error :" << controller.ErrorText()
                << std::endl;
    }
  }
  return 0;
}