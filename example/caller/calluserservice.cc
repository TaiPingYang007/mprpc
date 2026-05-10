#include "mprpcapplication.h"
#include "mprpcchannel.h"
#include "mprpccontroller.h"
#include "../user.pb.h"
#include <cstdlib>
#include <iostream>

namespace {
int LoadRepeatCount(const char *envName, int defaultValue) {
  const char *value = std::getenv(envName);
  if (value == nullptr || value[0] == '\0') {
    return defaultValue;
  }

  try {
    const int repeatCount = std::stoi(value);
    return repeatCount > 0 ? repeatCount : defaultValue;
  } catch (...) {
    return defaultValue;
  }
}
} // namespace

int main(int argc, char **argv) {
  // 整个程序启动以后，想使用mprpc框架享受rpc服务调用，一定要先调用初始化函数（只初始化一次）
  MprpcApplication::Init(argc, argv);

  //  演示调用远程发布的rpc方法Login
  fixbug::UserServiceRpc_Stub stub(new MprpcChannel());
  const int loginRepeat = LoadRepeatCount("MPRPC_LOGIN_REPEAT", 1);

  for (int i = 0; i < loginRepeat; ++i) {
    fixbug::LoginRequest request;
    request.set_name("zhang san");
    request.set_pwd("123456");

    fixbug::LoginResponse response;
    MprpcController loginController;
    stub.Login(&loginController, &request, &response, nullptr);

    if (loginController.Failed()) {
      std::cout << "rpc login response error：" << loginController.ErrorText()
                << std::endl;
      return 1;
    }

    if (response.result().errcode() != 0) {
      std::cout << "rpc login response error：" << response.result().errormasg()
                << std::endl;
      return 1;
    }

    if (loginRepeat == 1) {
      std::cout << "rpc login response success：" << response.sucess()
                << std::endl;
    } else {
      std::cout << "rpc login response success[" << (i + 1) << "/"
                << loginRepeat << "]：" << response.sucess() << std::endl;
    }
  }

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
