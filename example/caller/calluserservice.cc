#include "mprpcapplication.h"
#include "mprpcchannel.h"
#include "mprpccontroller.h"
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
  MprpcController login_controller;
  // 发起rpc方法的调用 同步rpc调用过程
  stub.Login(&login_controller, &request, &response,
             nullptr); // RpcChannel->RpcChannel::CallMethod
  // 集中来做所有rpc方法调用的参数序列化和网络发送

  // 一次rpc调用完成，读调用结果
  if (login_controller.Failed()) {
    std::cout << "rpc login response error：" << login_controller.ErrorText()
              << std::endl;
    return 1;
  }

  if (response.result().errcode() != 0) {
    std::cout << "rpc login response error：" << response.result().errormasg()
              << std::endl;
    return 1;
  }

  std::cout << "rpc login response success：" << response.sucess()
            << std::endl;

  //  演示调用远程发布的rpc方法Register
  fixbug::RegisterRequest registerRequest;
  registerRequest.set_id(1);
  registerRequest.set_name("li si");
  registerRequest.set_pwd("123456");

  fixbug::RegisterResponse registerResponse;
  MprpcController register_controller;
  stub.Register(&register_controller, &registerRequest, &registerResponse,
                nullptr);

  if (register_controller.Failed()) {
    std::cout << "rpc register response error:"
              << register_controller.ErrorText() << std::endl;
    return 1;
  }

  if (!registerResponse.sucess()) {
    std::cout << "rpc register response error:"
              << registerResponse.result().errormasg() << std::endl;
    return 1;
  }

  std::cout << "rpc register response success:" << registerResponse.sucess()
            << std::endl;

  return 0;
}
