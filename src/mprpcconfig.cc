#include "./include/mprpcconfig.h"
#include <cstdlib>
#include <iostream>
#include <string>

// 负责解析加载配置文件
void MprpcConfig::LoadConfigfile(const char *config_file) {
  FILE *pf = fopen(config_file, "r");
  if (pf == nullptr) {
    std::cout << config_file << "is not exist!\n";
    exit(EXIT_FAILURE);
  }

  // 1、注释  2、正确的配置项 （包含=）   3、去掉开头的多余的空格
  while (!feof(pf)) {
    char buf[512] = {0};
    fgets(buf, 512, pf);

    // 去掉字符串前面多余的空格
    std::string read_buf(buf);
    
    // 去掉多余的空格
    Trim(read_buf);
    
    // 判断#注释
    if (read_buf[0] == '#' || read_buf.empty()) {
      continue;
    }

    // 解析配置项
    int idx = read_buf.find('=');
    if (idx == -1) {
      // 配置项不合法
      continue;
    }

    std::string key;
    std::string value;

    key = read_buf.substr(0, idx);
    // key字符串这里是从0读取到 = 的位置，= 前可能有空格！key的最后几位可能是空格
    Trim(key);
    value = read_buf.substr(idx + 1, read_buf.size() - idx);
    // value字符串这里是从 = 读取到结尾的位置，= 后可能有空格！value的前几位可能是空格
    Trim(value);

    m_configMap.insert({key, value});
  }
}

// 查询配置项信息
std::string MprpcConfig::Load(const std::string &key) {
  auto it = m_configMap.find(key);
  if (it == m_configMap.end()) {
    return ""; // 没有找到对应的值，返回空字符串
  }
  return it->second;
}

// 去掉字符串后面的空格
void MprpcConfig::Trim(std::string &src_buf)
{
    int idx = src_buf.find_first_not_of(' ');
    if (idx != -1) {
      // 说明字符串前面有空格
      src_buf = src_buf.substr(idx, src_buf.size() - idx);
    }
    // 去掉字符串后面多余的空格
    idx = src_buf.find_last_not_of(" \n\r");    // \r是回车 \n是换行 " \n\r"（字符集合）可以去掉末尾的所有的空格、换行符、回车符
    if (idx != -1) {
      // 说明字符串后面有空格
      src_buf = src_buf.substr(0, idx + 1);
    }
}

