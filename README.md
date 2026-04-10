# mprpc — 轻量级分布式 RPC 框架

## Introduction

**mprpc** 是一个基于 C++11 从零实现的轻量级分布式 RPC（远程过程调用）框架，解决了微服务架构中跨进程通信繁琐、耦合度高的问题。开发者只需用 `.proto` 文件描述服务接口，框架即自动处理序列化、网络传输、服务注册与发现全流程，让远程调用像调本地函数一样简单。

---

## Tech Stack & Tools

| 层次 | 技术 | 说明 |
|------|------|------|
| 网络 I/O | [Muduo](https://github.com/chenshuo/muduo) | 基于 Reactor 模式的异步非阻塞 TCP 库 |
| 序列化 | [Protocol Buffers 3](https://protobuf.dev/) | Google 高效二进制序列化协议 |
| 服务发现 | [Apache ZooKeeper](https://zookeeper.apache.org/) | 分布式协调服务，管理服务注册与动态发现 |
| 构建系统 | CMake 3.10+ | 跨平台构建配置 |
| 语言标准 | C++11 | 线程、智能指针、条件变量等现代特性 |

---

## Key Features

### 1. 自定义二进制帧协议（TLV 结构）

通信层采用 **4 字节定长帧头 + 变长 Protobuf RpcHeader + 变长业务 Body** 的三段式 TLV 格式，避免了文本协议的解析开销，彻底消除 TCP 粘包/拆包问题。帧头仅携带 `service_name`、`method_name`、`args_size` 三个字段，极大压缩了协议冗余。

```
┌───────────────┬──────────────────────┬─────────────────┐
│  header_size  │      RpcHeader       │   Request Args  │
│   (4 bytes)   │  (Protobuf, 变长)    │  (Protobuf, 变长)│
└───────────────┴──────────────────────┴─────────────────┘
```

### 2. 基于 Muduo 的高性能事件驱动服务端

服务端（`RpcProvider`）直接集成 `muduo::net::TcpServer`，以 **one loop per thread** 模型驱动 I/O，天然支持高并发连接。服务注册通过反射机制实现：遍历 `ServiceDescriptor` 自动提取所有方法并建立路由表，新增 RPC 方法无需修改框架代码。

### 3. ZooKeeper 服务注册与动态发现

- **服务端启动时**：为每个方法在 ZooKeeper 创建**临时节点**（Ephemeral znode），路径为 `/ServiceName/MethodName`，值为 `ip:port`。进程宕机后临时节点自动删除，避免僵尸路由。  
- **客户端调用时**：`MprpcChannel` 通过 `zk_get` 实时查询目标地址，实现零配置的动态服务发现，支持横向扩缩容。

### 4. 异步日志系统（生产者-消费者 + LockQueue）

日志模块采用**双线程解耦**设计：业务线程将日志字符串 `Push` 到 `LockQueue<string>`（互斥锁 + 条件变量实现线程安全），独立 Logger 线程持续 `Pop` 并落盘，业务线程无需等待 I/O，日志写入对核心链路零阻塞。支持按日期自动切割日志文件。

---

## Prerequisites

| 依赖 | 最低版本 | 安装参考 |
|------|----------|----------|
| GCC / G++ | 7.0（支持 C++11） | `sudo apt install g++` |
| CMake | 3.10 | `sudo apt install cmake` |
| Protocol Buffers | 3.x | `sudo apt install libprotobuf-dev protobuf-compiler` |
| Muduo | — | 需手动编译，见 [muduo GitHub](https://github.com/chenshuo/muduo) |
| ZooKeeper C Client | 3.4+ | `sudo apt install libzookeeper-mt-dev` |

> Muduo 默认安装路径为 `/home/taipingyang/thirdparty/muduo`，如需更改，在构建时传入 `-DMUDUO_ROOT=<your_path>`。

---

## Build & Run

```bash
# 1. 克隆仓库
git clone <repo_url> && cd 03_rpc_framework

# 2. 一键构建（生成可执行文件到 bin/，框架库到 lib/）
chmod +x autobuild.sh && ./autobuild.sh

# 3. 启动 ZooKeeper（确保已安装并配置）
zkServer.sh start

# 4. 启动 RPC 服务端（新终端）
./bin/userservice -i bin/test.conf

# 5. 启动 RPC 客户端（新终端），发起远程调用
./bin/calluserservice -i bin/test.conf
```

如需自定义 Muduo 路径：

```bash
mkdir build && cd build
cmake .. -DMUDUO_ROOT=/your/muduo/path
make -j$(nproc)
```
