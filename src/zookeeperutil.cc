#include "zookeeperutil.h"
#include "logger.h"
#include "mprpcapplication.h"
#include <cerrno>
#include <cstring>
#include <iostream>
#include <semaphore.h>
#include <time.h>
#include <zookeeper/zookeeper.h>

namespace
{
  // 拿着一个 key（比如 "zk_timeout"），去配置文件里找对应的值，如果找不到就返回默认值
  int LoadTimeoutMs(const std::string &key, int defaultValue)
  {
    const std::string value =
        MprpcApplication::GetInstance().GetConfig().Load(key);
    if (value.empty())
    {
      return defaultValue; // 默认值
    }

    /*
      std::atoi (C 风格) VS	std::stoi (C++11 风格)
      std::atoi 是一个 C 风格的函数，接受一个 const char* 类型的字符串参数，并将其转换为整数。它在转换过程中不会抛出异常，如果转换失败会返回 0。
      std::stoi 是一个 C++11 风格的函数，接受一个 const std::string& 类型的字符串参数，并将其转换为整数。它在转换过程中会抛出 std::invalid_argument 异常（如果输入的字符串不是一个有效的整数）或 std::out_of_range 异常（如果输入的整数超出 int 类型的范围）。
    */

    try // 用于接收 std::stoi 可能抛出的异常，如果转换失败就返回默认值
    {
      const int timeoutMs = std::stoi(value);
      return timeoutMs > 0 ? timeoutMs : defaultValue;
    }
    catch (...)
    {
      return defaultValue;
    }
  }

  // 传入你要等的毫秒数 timeoutMs，返回一个 C 语言底层的 timespec 结构体。
  timespec BuildAbsoluteTimeout(int timeoutMs)
  {
    timespec absTimeout;
    /*
      大水桶（tv_sec）：装“秒”。
      小水桶（tv_nsec）：装“纳秒”（零头）。
    */
    clock_gettime(CLOCK_REALTIME, &absTimeout); // CLOCK_REALTIME 表示现实世界的真实时间

    absTimeout.tv_sec += timeoutMs / 1000;                                // 毫秒/1000 = 秒
    absTimeout.tv_nsec += static_cast<long>(timeoutMs % 1000) * 1000000L; // 毫秒%1000 = 毫秒的零头，乘以1000000L转换为纳秒
    if (absTimeout.tv_nsec >= 1000000000L)
    {
      absTimeout.tv_sec += 1;
      absTimeout.tv_nsec -= 1000000000L;
    }

    return absTimeout;
  }
} // namespace

// 全局的watcher观察器函数，zkserver发生变化时会调用这个函数
// zkserver给zkclient的通知
void global_watcher(zhandle_t *zh, int type, int state, const char *path,
                    void *watcherCtx)
{
  (void)path;
  (void)watcherCtx;
  if (type == ZOO_SESSION_EVENT) // 回调的消息类型是和会话相关的消息类型
  {
    if (state == ZOO_CONNECTED_STATE) // zkclient成功连接上zkserver了
    {
      // 将set_context把一个“自定义指针”挂到 ZooKeeper 句柄上的“自定义指针”取出来
      sem_t *sem = (sem_t *)zoo_get_context(
          zh);       // 从zkclient获取上下文参数，这里是信号量的地址
      sem_post(sem); // 释放信号量，通知Start函数继续执行
    }
  }
}

// 构造函数
ZkClient::ZkClient() : m_zhandle(nullptr) {}

// 析构函数
ZkClient::~ZkClient()
{
  if (m_zhandle != nullptr)
  {
    zookeeper_close(m_zhandle); // 关闭zkclient连接，释放资源
  }
}

// zkclient启动连接zkserver
bool ZkClient::Start()
{
  if (m_zhandle != nullptr)
  {
    return true;
  }

  // 从配置文件加载zkserver的ip和端口号
  const std::string host =
      MprpcApplication::GetInstance().GetConfig().Load("zookeeperip");
  const std::string port =
      MprpcApplication::GetInstance().GetConfig().Load("zookeeperport");
  const std::string connstr = host + ":" + port;
  const int connectTimeoutMs = LoadTimeoutMs("zookeeperconnecttimeout", 5000);
  const int sessionTimeoutMs = LoadTimeoutMs("zookeepersessiontimeout", 30000);

  /*
      zookeeper_mt：多线程版本
      zookeeper的API客户端程序提供了三个线程
      API调用线程 zookeeper_init
      网络I/O线程 pthread_create，底层用poll实现 处理网络I/O事件
      watcher回调线程 处理watcher事件
      这三个线程之间通过条件变量和互斥锁进行同步和通信，保证线程安全
  */
  /*
      zookeeper_init函数的参数说明：
      const char *host: zkserver的ip和端口号，格式为"ip:port"
      watcher_fn fn: 全局的watcher观察器函数，zkserver发生变化时会调用这个函数
      int recv_timeout: 会话超时时间，单位为毫秒
      const clientid_t *clientid: 客户端id，通常设置为nullptr，表示新建一个会话
      void *context:
     上下文参数，可以在watcher函数中通过zoo_get_context获取，通常设置为nullptr
      int flags: 连接选项，通常设置为0
      返回值：成功时返回一个指向zhandle_t结构的指针，失败时返回nullptr，并且errno会被设置为错误码
   */
  // 连接zkserver，会话超时时间设置为30000ms，超过30000mszk服务器会自动断开，连接成功后会返回一个zkclient句柄
  m_zhandle = zookeeper_init(connstr.c_str(), global_watcher, sessionTimeoutMs,
                             nullptr,
                             nullptr, 0);
  // 判断zkclient句柄创建是否成功
  if (m_zhandle == nullptr)
  {
    std::cerr << "zookeeper_init error!" << std::endl;
    LOG_ERROR("zookeeper_init error! connstr=%s", connstr.c_str());
    return false;
  }

  sem_t sem;            // 定义一个信号量
  sem_init(&sem, 0, 0); // 初始化信号量，初始值为0
  // 把一个“自定义指针”挂到 ZooKeeper 句柄上。
  zoo_set_context(m_zhandle,
                  &sem); // 将信号量的地址作为上下文参数传递给zkclient

  const timespec absTimeout = BuildAbsoluteTimeout(connectTimeoutMs);
  const int waitResult = sem_timedwait(&sem, &absTimeout);
  zoo_set_context(m_zhandle, nullptr);
  sem_destroy(&sem);
  if (waitResult != 0)
  {
    std::cerr << "zookeeper connect timeout after " << connectTimeoutMs
              << " ms, reason: " << std::strerror(errno) << std::endl;
    LOG_ERROR("zookeeper connect timeout after %d ms, connstr=%s, reason=%s",
              connectTimeoutMs, connstr.c_str(), std::strerror(errno));
    // 超时后关闭句柄
    zookeeper_close(m_zhandle);
    m_zhandle = nullptr;
    return false;
  }

  std::cout << "zkclient start success!" << std::endl;
  LOG_INFO("zkclient start success! connstr=%s", connstr.c_str());
  return true;
}

// 在zkserver上根据指定的path创建znode节点  ps：默认是0为永久性节点
void ZkClient::Create(const char *path, const char *data, int datalen, int state)
{
  if (m_zhandle == nullptr)
  {
    std::cerr << "zookeeper client is not connected, create failed. path: "
              << path << std::endl;
    LOG_ERROR("zookeeper client is not connected, create failed. path=%s",
              path);
    return;
  }

  char path_buffer[128];                // 存储创建的znode节点的路径
  int buffer_len = sizeof(path_buffer); // 路径缓冲区的长度

  int flag = zoo_exists(ZkClient::m_zhandle, path, 0,
                        nullptr); // 判断path路径对应的znode节点是否存在

  // 先判断path路径对应的znode节点是否存在，如果不存在就创建，如果存在就不创建了
  if (flag == ZNONODE) // znode节点不存在，可以创建
  {
    // 创建指定path的znode节点
    flag = zoo_create(ZkClient::m_zhandle, path, data, datalen, &ZOO_OPEN_ACL_UNSAFE,
                      state, path_buffer, buffer_len);
    if (flag == ZOK)
    {
      std::cout << "znode create success, path: " << path_buffer << std::endl;
      LOG_INFO("znode create success, path=%s", path_buffer);
    }
    else
    {
      std::cout << "flag: " << flag << std::endl;
      std::cerr << "znode create error, path: " << path << std::endl;
      LOG_ERROR("znode create error, path=%s, flag=%d", path, flag);
      exit(EXIT_FAILURE);
    }
  }
}

// 根据参数指定的znode节点路径，获取znode节点的值
std::string ZkClient::GetData(const char *path)
{
  if (m_zhandle == nullptr)
  {
    std::cerr << "zookeeper client is not connected, get data failed. path: "
              << path << std::endl;
    LOG_ERROR("zookeeper client is not connected, get data failed. path=%s",
              path);
    return "";
  }

  char buffer[64] = {0};           // 存储获取到的znode节点的值
  int buffer_len = sizeof(buffer); // 值缓冲区的长度

  // zoo_get由zookeeper提供的API函数，根据参数指定的znode节点路径，获取znode节点的值，返回值flag表示获取结果，获取成功时返回ZOK，获取失败时返回错误码
  int flag = zoo_get(m_zhandle, path, 0, buffer, &buffer_len, nullptr); // buffer_len会被修改为实际获取到的值的长度
  if (flag != ZOK)
  {
    std::cerr << "znode get error, path: " << path << std::endl;
    LOG_ERROR("znode get error, path=%s, flag=%d", path, flag);
    return ""; // 获取失败，返回空字符串
  }
  else
  {
    LOG_INFO("znode get success, path=%s", path);
    return std::string(buffer, buffer_len); // 返回获取到的znode节点的值
  }
}
