#include "zookeeperutil.h"
#include "mprpcapplication.h"
#include <iostream>
#include <semaphore.h>
#include <zookeeper/zookeeper.h>

// 全局的watcher观察器函数，zkserver发生变化时会调用这个函数
// zkserver给zkclient的通知
void global_watcher(zhandle_t *zh, int type, int state, const char *path,
                    void *watcherCtx) {
  if (type == ZOO_SESSION_EVENT) // 回调的消息类型是和会话相关的消息类型
  {
    if (state == ZOO_CONNECTED_STATE) // zkclient成功连接上zkserver了
    {
      sem_t *sem = (sem_t *)zoo_get_context(
          zh); // 从zkclient获取上下文参数，这里是信号量的地址
      sem_post(sem); // 释放信号量，通知Start函数继续执行
    }
  }
}

// 构造函数
ZkClient::ZkClient() : m_zhandle(nullptr) {}

// 析构函数
ZkClient::~ZkClient() {
  if (m_zhandle != nullptr) {
    zookeeper_close(m_zhandle); // 关闭zkclient连接，释放资源
  }
}

// zkclient启动连接zkserver
void ZkClient::Start() {
  // 从配置文件加载zkserver的ip和端口号
  std::string host =
      MprpcApplication::GetInstance().GetConfig().Load("zookeeperip");
  std::string port =
      MprpcApplication::GetInstance().GetConfig().Load("zookeeperport");
  std::string connstr = host + ":" + port;

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
  // 连接zkserver，超时时间设置为30000ms，连接成功后会返回一个zkclient句柄
  m_zhandle = zookeeper_init(connstr.c_str(), global_watcher, 30000, nullptr,
                             nullptr, 0);
  // 判断zkclient句柄创建是否成功
  if (m_zhandle == nullptr) {
    std::cerr << "zookeeper_init error!" << std::endl;
    exit(EXIT_FAILURE);
  }

  sem_t sem;            // 定义一个信号量
  sem_init(&sem, 0, 0); // 初始化信号量，初始值为0
  zoo_set_context(m_zhandle,
                  &sem); // 将信号量的地址作为上下文参数传递给zkclient

  sem_wait(&sem); // 等待信号量，阻塞当前线程
  std::cout << "zkclient start success!" << std::endl;
}

// 在zkserver上根据指定的path创建znode节点  ps：默认是0为永久性节点
void ZkClient::Create(const char *path, const char *data, int datalen, int state) {
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
    if (flag == ZOK) {
      std::cout << "znode create success, path: " << path_buffer << std::endl;
    } else {
      std::cout << "flag: " << flag << std::endl;
      std::cerr << "znode create error, path: " << path << std::endl;
      exit(EXIT_FAILURE);
    }
  }
}

// 根据参数指定的znode节点路径，获取znode节点的值
std::string ZkClient::GetData(const char *path)
{
    char buffer[64];                // 存储获取到的znode节点的值
    int buffer_len = sizeof(buffer); // 值缓冲区的长度

    int flag = zoo_get(m_zhandle, path, 0, buffer, &buffer_len, nullptr);
    if (flag != ZOK) {
      std::cerr << "znode get error, path: " << path << std::endl;
      return ""; // 获取失败，返回空字符串
    } else
    {
        return buffer; // 返回获取到的znode节点的值
    }
}