#include "./include/mprpcapplication.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>

// 初始化构造函数
MprpcApplication::MprpcApplication() {} // 空实现构造函数,避免编译器报错
// 初始化静态成员变量
MprpcConfig MprpcApplication::m_config;

void ShowArgsHelp() {
  std::cout << "format: command -i <configfile>" << std::endl;
}

void MprpcApplication::Init(int argc, char **argv) {
  if (argc < 2) {
    ShowArgsHelp();
    exit(EXIT_FAILURE);
  }

  // 利用getopt读参数
  int c = 0;
  std::string config_file;
  while ((c = getopt(argc, argv, "i:")) != -1) {
    switch (c) {
    case 'i':
      config_file = optarg; // optarg：这是getopt自带的一个全局指针！当 getopt
                            // 看到 -i test.conf 时，它不仅返回 'i'，还会偷偷把
                            // "test.conf" 的地址塞进 optarg 这个全局变量里。
      break;
    case '?':
      ShowArgsHelp();
      // 配置不正确，直接退出
      exit(EXIT_FAILURE);
    case ':':
      // 出现了 -i 后面没有配置参数
      ShowArgsHelp();
      exit(EXIT_FAILURE);
    default:
      break;
    }
  }

  // 开始加载配置文件 rpcserver_ip = rpcserver_port = zookeeper_ip =
  // zookeeper_port =
  m_config.LoadConfigfile(config_file.c_str());
}

// 类外实现无需static参数
MprpcApplication &MprpcApplication::GetInstance() {
  static MprpcApplication app;
  return app;
}

MprpcConfig &MprpcApplication::GetConfig() { return m_config; }