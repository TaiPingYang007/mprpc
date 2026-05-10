#include "./include/mprpcapplication.h"
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
}

MprpcApplication &MprpcApplication::GetInstance() {
  static MprpcApplication app;
  return app;
}

MprpcConfig &MprpcApplication::GetConfig() { return m_config; }
