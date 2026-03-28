#pragma once
#include "mprpcconfig.h"

// mprpac框架的基础类，负责框架的初始化和销毁
class MprpcApplication {
public:
  static void Init(int argc, char **argv);

  static MprpcApplication &GetInstance();

  static MprpcConfig &GetConfig();
private:
  static MprpcConfig m_config; // 普通的静态成员变量不能访问静态成员函数

  MprpcApplication();
  MprpcApplication(const MprpcApplication &) = delete;
  MprpcApplication(MprpcApplication &&) = delete;
};