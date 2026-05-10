#pragma once
#include <string>
#include <unordered_map>

// 框架运行配置读取类
class MprpcConfig {
public:
  // 负责解析加载配置文件，文件可选
  void LoadConfigfile(const char *config_file);

  // 查询配置项信息，优先级：环境变量 > 配置文件 > 默认值
  std::string Load(const std::string &key);

private:
  std::unordered_map<std::string, std::string> m_configMap;
  void Trim(std::string &src_buf);
  static std::string NormalizeKey(const std::string &key);
};
