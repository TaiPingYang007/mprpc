#include <iostream>
#include <string>
#include "../user.pb.h"
/*
UserService 原来是一个本地服务，提供了两个进程的本地方法，Login和GetFriendList
*/

class UserService : public fixbug::UserServiceRpc    // 使用在rpc服务提供者
{
public:
    bool Login(std::string name , std::string pwd)
    {
        std::cout << "doing local service :Login\n";
        std::cout << "name:" << name << "password:" << pwd << "\n"; 

        return true;
    }

    /* 
    重写基类UserServiceRpc虚函数，下面这些方法都是框架直接调用
        1、caller ==> Login(LoginRequest) ==> muduo ==> callee Login(LoginRequest)
        2、callee ==> Login(LoginRequest) ==> 交到下面重写的这个Login方法上了
    */
    void Login(::google::protobuf::RpcController* controller,
                       const ::fixbug::LoginRequest* request,
                       ::fixbug::LoginResponse* response,
                       ::google::protobuf::Closure* done)
                       {}
};