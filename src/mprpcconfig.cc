#include "./include/mprpcconfig.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace {
const std::unordered_map<std::string, std::string> &DefaultConfig() {
  static const std::unordered_map<std::string, std::string> defaults = {
      {"RPC_BIND_IP", "0.0.0.0"},
      {"RPC_PORT", "8000"},
      {"RPC_ADVERTISE_HOST", "127.0.0.1"},
      {"RPC_IO_THREADS", "4"},
      {"ZK_ENDPOINTS", "127.0.0.1:2181"},
      {"MPRPC_ZK_NAMESPACE", "mprpc"},
      {"ZK_CONNECT_TIMEOUT_MS", "5000"},
      {"ZK_SESSION_TIMEOUT_MS", "30000"},
      {"RPC_CONNECT_TIMEOUT_MS", "3000"},
      {"RPC_SEND_TIMEOUT_MS", "3000"},
      {"RPC_RECV_TIMEOUT_MS", "5000"},
      {"MPRPC_LOG_MODE", "stdout"},
      {"MPRPC_LOG_DIR", "logs"},
  };
  return defaults;
}

const std::unordered_map<std::string, std::string> &LegacyKeyMap() {
  static const std::unordered_map<std::string, std::string> aliases = {
      {"rpcserverip", "RPC_BIND_IP"},
      {"rpcserverport", "RPC_PORT"},
      {"rpcadvertisehost", "RPC_ADVERTISE_HOST"},
      {"rpciothreads", "RPC_IO_THREADS"},
      {"zookeeperip", "ZK_ENDPOINTS"},
      {"zookeeperport", "ZK_ENDPOINTS"},
      {"zookeeperconnecttimeout", "ZK_CONNECT_TIMEOUT_MS"},
      {"zookeepersessiontimeout", "ZK_SESSION_TIMEOUT_MS"},
      {"rpcconnecttimeout", "RPC_CONNECT_TIMEOUT_MS"},
      {"rpcsendtimeout", "RPC_SEND_TIMEOUT_MS"},
      {"rpcrecvtimeout", "RPC_RECV_TIMEOUT_MS"},
      {"mprpczookeepernamespace", "MPRPC_ZK_NAMESPACE"},
      {"mprpclogmode", "MPRPC_LOG_MODE"},
      {"mprpclogdir", "MPRPC_LOG_DIR"},
  };
  return aliases;
}

std::vector<std::string> LegacyKeysForNormalized(
    const std::string &normalizedKey) {
  std::vector<std::string> keys;
  for (std::unordered_map<std::string, std::string>::const_iterator it =
           LegacyKeyMap().begin();
       it != LegacyKeyMap().end(); ++it) {
    if (it->second == normalizedKey) {
      keys.push_back(it->first);
    }
  }
  return keys;
}

std::string ToUpper(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::toupper(ch));
                 });
  return value;
}

std::string BuildZkEndpointsFromLegacy(const std::unordered_map<std::string, std::string> &configMap) {
  std::string host = "127.0.0.1";
  std::string port = "2181";

  std::unordered_map<std::string, std::string>::const_iterator hostIt =
      configMap.find("zookeeperip");
  if (hostIt != configMap.end() && !hostIt->second.empty()) {
    host = hostIt->second;
  }

  std::unordered_map<std::string, std::string>::const_iterator portIt =
      configMap.find("zookeeperport");
  if (portIt != configMap.end() && !portIt->second.empty()) {
    port = portIt->second;
  }

  return host + ":" + port;
}
} // namespace

void MprpcConfig::LoadConfigfile(const char *config_file) {
  if (config_file == nullptr || config_file[0] == '\0') {
    return;
  }

  FILE *pf = fopen(config_file, "r");
  if (pf == nullptr) {
    std::cerr << "config file not found, continue with env/defaults: "
              << config_file << std::endl;
    return;
  }

  char buf[512] = {0};
  while (fgets(buf, sizeof(buf), pf) != nullptr) {
    std::string line(buf);
    Trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }

    const std::string::size_type idx = line.find('=');
    if (idx == std::string::npos) {
      continue;
    }

    std::string key = line.substr(0, idx);
    std::string value = line.substr(idx + 1);
    Trim(key);
    Trim(value);

    if (!key.empty()) {
      m_configMap[key] = value;
    }
  }

  fclose(pf);
}

std::string MprpcConfig::Load(const std::string &key) {
  const std::string normalizedKey = NormalizeKey(key);

  const char *envValue = std::getenv(normalizedKey.c_str());
  if (envValue != nullptr && envValue[0] != '\0') {
    return envValue;
  }

  if (normalizedKey == "ZK_ENDPOINTS") {
    const char *legacyHost = std::getenv("ZOOKEEPERIP");
    const char *legacyPort = std::getenv("ZOOKEEPERPORT");
    if (legacyHost != nullptr && legacyHost[0] != '\0') {
      const std::string port =
          (legacyPort != nullptr && legacyPort[0] != '\0') ? legacyPort : "2181";
      return std::string(legacyHost) + ":" + port;
    }
  }

  if (normalizedKey == "RPC_ADVERTISE_HOST") {
    const char *legacyAdvertise = std::getenv("RPCSERVERIP");
    if (legacyAdvertise != nullptr && legacyAdvertise[0] != '\0') {
      return legacyAdvertise;
    }
  }

  std::unordered_map<std::string, std::string>::const_iterator fileIt =
      m_configMap.find(normalizedKey);
  if (fileIt != m_configMap.end() && !fileIt->second.empty()) {
    return fileIt->second;
  }

  const std::vector<std::string> legacyKeys =
      LegacyKeysForNormalized(normalizedKey);
  for (std::vector<std::string>::const_iterator it = legacyKeys.begin();
       it != legacyKeys.end(); ++it) {
    std::unordered_map<std::string, std::string>::const_iterator rawIt =
        m_configMap.find(*it);
    if (rawIt != m_configMap.end() && !rawIt->second.empty()) {
      if (normalizedKey == "ZK_ENDPOINTS") {
        return BuildZkEndpointsFromLegacy(m_configMap);
      }
      return rawIt->second;
    }
  }

  if (normalizedKey == "ZK_ENDPOINTS") {
    std::unordered_map<std::string, std::string>::const_iterator legacyHostIt =
        m_configMap.find("zookeeperip");
    if (legacyHostIt != m_configMap.end()) {
      return BuildZkEndpointsFromLegacy(m_configMap);
    }
  }

  if (normalizedKey == "RPC_ADVERTISE_HOST") {
    std::unordered_map<std::string, std::string>::const_iterator legacyBindIt =
        m_configMap.find("rpcserverip");
    if (legacyBindIt != m_configMap.end() && !legacyBindIt->second.empty()) {
      return legacyBindIt->second;
    }
  }

  std::unordered_map<std::string, std::string>::const_iterator defaultIt =
      DefaultConfig().find(normalizedKey);
  if (defaultIt != DefaultConfig().end()) {
    return defaultIt->second;
  }

  return "";
}

void MprpcConfig::Trim(std::string &src_buf) {
  const std::string::size_type begin = src_buf.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    src_buf.clear();
    return;
  }

  const std::string::size_type end = src_buf.find_last_not_of(" \t\r\n");
  src_buf = src_buf.substr(begin, end - begin + 1);
}

std::string MprpcConfig::NormalizeKey(const std::string &key) {
  if (key.empty()) {
    return key;
  }

  std::unordered_map<std::string, std::string>::const_iterator aliasIt =
      LegacyKeyMap().find(key);
  if (aliasIt != LegacyKeyMap().end()) {
    return aliasIt->second;
  }

  return ToUpper(key);
}
