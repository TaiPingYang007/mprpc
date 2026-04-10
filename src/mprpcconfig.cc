#include "./include/mprpcconfig.h"
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <string>

// 负责解析加载配置文件
void MprpcConfig::LoadConfigfile(const char *config_file) {
  FILE *pf = fopen(config_file, "r");
  if (pf == nullptr) {
    std::cout << config_file << " is not exist!\n";
    exit(EXIT_FAILURE);
  }

  // 1、注释  2、正确的配置项 （包含=）3、去掉开头和结尾以及中间的空格
  char buf[512] = {0};
  while (fgets(buf, sizeof(buf), pf) != nullptr) {
    // 做数据缓冲
    std::string line(buf);

    // 去掉多余的空格
    Trim(line);

    // 判断#注释
    if (line.empty() || line[0] == '#') {
      continue;
    }

    // 解析配置项
    const std::string::size_type idx = line.find('=');
    if (idx == std::string::npos) {
      // 配置项不合法
      continue;
    }

    std::string key = line.substr(0, idx);
    // key字符串这里是从0读取到 = 的位置，=
    // 前可能有空格！key的最后几位可能是空格
    Trim(key);
    std::string value = line.substr(idx + 1);
    // value字符串这里是从 = 读取到结尾的位置，=
    // 后可能有空格！value的前几位可能是空格
    Trim(value);

    m_configMap[key] = value;
  }

  fclose(pf);
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
void MprpcConfig::Trim(std::string &src_buf) {
  const std::string::size_type begin = src_buf.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    src_buf.clear();
    return;
  }

  const std::string::size_type end = src_buf.find_last_not_of(" \t\r\n");
  src_buf = src_buf.substr(begin, end - begin + 1);
}
