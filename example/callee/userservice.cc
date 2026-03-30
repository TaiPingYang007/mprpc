#include "../../src/include/mprpcapplication.h"
#include "../../src/include/rpcprovider.h"
#include "../user.pb.h"
#include <iostream>
#include <string>
/*
UserService 原来是一个本地服务，提供了两个进程的本地方法，Login和GetFriendList
*/

class UserService : public fixbug::UserServiceRpc // 使用在rpc服务提供者
{
public:
  bool Login(std::string name, std::string pwd) {
    std::cout << "doing local service :Login\n";
    std::cout << "name:" << name << "password:" << pwd << "\n";

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
             ::google::protobuf::Closure *done) {
    // 框架给业务上报了请求参数
    // LoginRequest，应用获取相应数据做本地业务
    std::string name = request->name();
    std::string pwd = request->pwd();

    // 做本地业务
    bool login_request = Login(name, pwd);

    //   把响应写入LoginResponse 包括错误码、错误消息、返回值
    fixbug::ResultCode *code = response->mutable_result();
    code->set_errcode(0);
    code->set_errormasg("");
    response->set_sucess(login_request);

    // 执行回调操作
    done->Run();
  }
};

int main(int argc, char **argv) {
  // 调用框架的初始化操作 启动需要读配置文件  provider -i config.conf
  MprpcApplication::Init(argc, argv);

  // provider是一个rpc网络服务对象 把UserService对象发布到rpc节点上
  RpcProvider provider;
  provider.NotifyService(new UserService);

  // 启动一个rpc服务发布节点 Run以后，进入阻塞状态，等待远程的rpc调用请求
  provider.Run();

  return 0;
}