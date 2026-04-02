#pragma once
#include "./mprpcconfig.h"

// mprpc 框架的基础类，负责框架的初始化和销毁
class MprpcApplication {
public:
  static void Init(int argc, char **argv);

  static MprpcApplication &GetInstance();

  static MprpcConfig &GetConfig();

private:
  static MprpcConfig m_config; // 静态成员函数，不能访问普通的成员变量

  MprpcApplication();
  MprpcApplication(const MprpcApplication &) = delete;
  MprpcApplication(MprpcApplication &&) = delete;
};
