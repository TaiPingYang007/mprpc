#pragma once

#include <semaphore.h>  // 信号量
#include <zookeeper/zookeeper.h>
#include <string>

// 封装的zk客户端类
class ZkClient
{
public:
    ZkClient();
    ~ZkClient();
    // zkclient启动连接zkserver
    bool Start();
    // 在zkserver上根据指定的path创建znode节点  ps：默认是0为永久性节点
    void Create(const char *path, const char *data, int datalen, int state=0);
    // 根据参数指定的znode节点路径，获取znode节点的值
    std::string GetData(const char *path);
private:
    // zk的客户端句柄（zkclient连接），通过这个句柄就可以操作zkserver
    zhandle_t *m_zhandle;
}; 
