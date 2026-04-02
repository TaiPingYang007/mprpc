#include "mprpcapplication.h"
#include "mprpcchannel.h"
#include "../user.pb.h"
#include <iostream>

int main(int argc, char **argv) {
  // 整个程序启动以后，想使用mprpc框架享受rpc服务调用，一定要先调用初始化函数（只初始化一次）
  MprpcApplication::Init(argc, argv);

  //  演示调用远程发布的rpc方法Login
  fixbug::UserServiceRpc_Stub stub(new MprpcChannel());

  // rpc请求参数
  fixbug::LoginRequest request;
  request.set_name("zhang san");
  request.set_pwd("123456");
  // rpc方法的响应
  fixbug::LoginResponse response;
  // 发起rpc方法的调用 同步rpc调用过程
  stub.Login(nullptr, &request, &response,
             nullptr); // RpcChannel->RpcChannel::CallMethod
  // 集中来做所有rpc方法调用的参数序列化和网络发送

  // 一次rpc调用完成，读调用结果
  if (response.result().errcode() == 0) {
    // 调用成功
    std::cout << "rpc login response success：" << response.sucess()
              << std::endl;
  } else {
    std::cout << "rpc login response error：" << response.result().errormasg()
              << std::endl;
  }

  //  演示调用远程发布的rpc方法Register
  fixbug::RegisterRequest request01;
  request01.set_id(01);
  request01.set_name("li si");
  request01.set_pwd("123456");

  fixbug::RegisterResponse response01;
  stub.Register(nullptr, &request01, &response01, nullptr);

  if (response01.sucess()) {
    std::cout << "rpc register response success:" << response01.sucess()
              << std::endl;
  } else {
    std::cout << "rpc register response error:"
              << response01.result().errormasg() << std::endl;
  }

  return 0;
}