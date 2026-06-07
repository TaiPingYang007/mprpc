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

  RPC_LOG_INFO("first log!");
  RPC_LOG_ERROR("%s:%s:%d", __FILE__, __FUNCTION__, __LINE__);

  // rpc框架初始化
  MprpcApplication::Init(argc, argv);

  // 注册服务
  RpcProvider provider;
  provider.NotifyService(new FriendService());

  // 启动服务
  provider.Run();

  // 注意：provider.Run() 内部是 muduo 事件循环死循环，正常运行下不会返回，
  // 因此这里其实不可达。真正的服务端优雅退出应在信号处理（如 SIGINT/SIGTERM）
  // 里先停事件循环、再调用 Shutdown()，本次范围不含信号处理。
  // 保留这行用于标明日志的关闭接入点：排空队列、flush、fclose、join 消费线程。
  RpcLogger::GetInstance().Shutdown();

  return 0;
}
