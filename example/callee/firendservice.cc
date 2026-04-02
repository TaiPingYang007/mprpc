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
  std::vector<User> GetFriendList(uint32_t id) {
    std::cout << "do GetFriendList service! userid:" << id << std::endl;
    std::vector<User> vec;
    vec.push_back({1, "zhang san"});
    vec.push_back({2, "li si"});
    vec.push_back({3, "wang wu"});
    return vec;
  }

  // 重写基类方法
  void GetFriendList(::google::protobuf::RpcController *controller,
                     const ::fixbug::GetFriendListRequest *request,
                     ::fixbug::GetFriendListResponse *response,
                     ::google::protobuf::Closure *done) {
    uint32_t userid = request->userid();
    std::vector<User> FriendList = GetFriendList(userid);

    response->mutable_result()->set_errcode(0);
    response->mutable_result()->set_errmsg("");
    for (User &user : FriendList) {
      fixbug::User *_user = response->add_friend_list();
      _user->set_userid(user.userid);
      _user->set_name(user.name);
    }
    done->Run();
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