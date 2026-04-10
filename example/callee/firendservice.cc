#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "../friend.pb.h"
#include "logger.h"
#include "mprpcapplication.h"
#include "mprpcprovider.h"

// 定义User结构体
struct User {
  uint32_t userid;
  std::string name;
};

class FriendService : public fixbug::FriendServiceRpc {
public:
  // 获取好友列表方法
  std::vector<User> GetFriendListFromLocal(uint32_t userId) {
    std::cout << "do GetFriendList service! userid:" << userId << std::endl;

    std::vector<User> friendList;
    friendList.push_back({1, "zhang san"});
    friendList.push_back({2, "li si"});
    friendList.push_back({3, "wang wu"});
    return friendList;
  }

  // 重写基类方法
  void GetFriendList(::google::protobuf::RpcController *controller,
                     const ::fixbug::GetFriendListRequest *request,
                     ::fixbug::GetFriendListResponse *response,
                     ::google::protobuf::Closure *done) override {
    (void)controller;
    const uint32_t userId = request->userid();
    const std::vector<User> friendList = GetFriendListFromLocal(userId);

    response->mutable_result()->set_errcode(0);
    response->mutable_result()->set_errmsg("");

    for (std::vector<User>::const_iterator it = friendList.begin();
         it != friendList.end(); ++it) {
      fixbug::User *user = response->add_friend_list();
      user->set_userid(it->userid);
      user->set_name(it->name);
    }

    if (done != nullptr) {
      done->Run();
    }
  }
};

int main(int argc, char **argv) {

  LOG_INFO("first log!");
  LOG_ERROR("%s:%s:%d", __FILE__, __FUNCTION__, __LINE__);

  // rpc框架初始化
  MprpcApplication::Init(argc, argv);

  // 注册服务
  RpcProvider provider;
  provider.NotifyService(new FriendService());

  // 启动服务
  provider.Run();

  return 0;
}
