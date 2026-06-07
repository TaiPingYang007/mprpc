#include "./include/mprpcapplication.h"
#include "./include/logger.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {
void ShowArgsHelp() {
  std::cout << "format: command [-i <configfile>]" << std::endl;
}
} // namespace

MprpcApplication::MprpcApplication() {}
MprpcConfig MprpcApplication::m_config;

void MprpcApplication::Init(int argc, char **argv) {
  optind = 1;

  int c = 0;
  std::string configFile;
  while ((c = getopt(argc, argv, "hi:")) != -1) {
    switch (c) {
    case 'i':
      configFile = optarg;
      break;
    case 'h':
      ShowArgsHelp();
      exit(EXIT_SUCCESS);
    case '?':
    case ':':
      ShowArgsHelp();
      exit(EXIT_FAILURE);
    default:
      break;
    }
  }

  if (!configFile.empty()) {
    m_config.LoadConfigfile(configFile.c_str());
  }

  // 配置（env/默认/配置文件）此刻已全部就绪，把日志配置一次性 latch 进 logger。
  // 此后日志消费线程不再每行读配置，且配置文件里的设置也能正确生效。
  RpcLogger::GetInstance().Configure();
}

MprpcApplication &MprpcApplication::GetInstance() {
  static MprpcApplication app;
  return app;
}

MprpcConfig &MprpcApplication::GetConfig() { return m_config; }
