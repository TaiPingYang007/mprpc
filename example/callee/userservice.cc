#include "../user.pb.h"
#include "mprpcapplication.h"
#include "mprpcprovider.h"
#include <iostream>
#include <string>

/*
  UserService 原来是一个本地服务，提供了两个进程的本地方法，Login和GetFriendList
*/

class UserService : public fixbug::UserServiceRpc // 使用在rpc服务提供者
{
public:
  bool LoginLocal(const std::string &name, const std::string &pwd) {
    std::cout << "doing local service :Login\n";
    std::cout << "name:" << name << "password:" << pwd << "\n";

    return true;
  }

  bool RegisterLocal(uint32_t id, const std::string &name,
                     const std::string &pwd) {
    std::cout << "doing local service :Register\n";
    std::cout << "id:" << id << "name:" << name << "password:" << pwd << "\n";
    return true;
  }

  /*
  重写基类UserServiceRpc虚函数，下面这些方法都是框架直接调用
      1、caller ==> Login(LoginRequest) ==> muduo ==> callee Login(LoginRequest)
      2、callee ==> Login(LoginRequest) ==> 交到下面重写的这个Login方法上了
  */
  void Login(::google::protobuf::RpcController *controller,
             const ::fixbug::LoginRequest *request,
             ::fixbug::LoginResponse *response,
             ::google::protobuf::Closure *done) override {
    (void)controller;
    // 框架给业务上报了请求参数
    // LoginRequest，应用获取相应数据做本地业务
    const std::string name = request->name();
    const std::string pwd = request->pwd();

    // 做本地业务
    const bool loginSuccess = LoginLocal(name, pwd);

    //   把响应写入LoginResponse 包括错误码、错误消息、返回值
    fixbug::ResultCode *result = response->mutable_result();
    result->set_errcode(0);
    result->set_errormasg("");
    response->set_sucess(loginSuccess);

    // 执行回调操作
    if (done != nullptr) {
      done->Run();
    }
  }

  void Register(::google::protobuf::RpcController *controller,
                const ::fixbug::RegisterRequest *request,
                ::fixbug::RegisterResponse *response,
                ::google::protobuf::Closure *done) override {
    (void)controller;
    // 框架给业务上报了请求参数
    // LoginRequest，应用获取相应数据做本地业务
    const uint32_t id = request->id();
    const std::string name = request->name();
    const std::string pwd = request->pwd();

    // 做本地业务
    const bool registerSuccess = RegisterLocal(id, name, pwd);

    // 把执行结果返回response
    response->mutable_result()->set_errcode(0);
    response->mutable_result()->set_errormasg("");
    response->set_sucess(registerSuccess);

    // 执行回调
    if (done != nullptr) {
      done->Run();
    }
  }
};

int main(int argc, char **argv) {
  // 调用框架的初始化操作 启动需要读配置文件  provider -i config.conf
  MprpcApplication::Init(argc, argv);

  // provider是一个rpc网络服务对象 把UserService对象发布到rpc节点上
  RpcProvider provider;
  provider.NotifyService(new UserService());

  // 启动一个rpc服务发布节点 Run以后，进入阻塞状态，等待远程的rpc调用请求
  provider.Run();

  return 0;
}
