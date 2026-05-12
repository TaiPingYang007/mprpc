# mprpc

一个基于 `C++11 + Muduo + Protobuf + ZooKeeper` 的轻量级 RPC 框架练手项目。

这份仓库现在采用 **模式 A：本地写代码 + 本地编译运行 C++ 程序 + Docker 只跑依赖服务**。

也就是说：

- 代码在本地开发
- C++ 程序在本地编译
- `userservice`、`friendservice`、`calluserservice`、`callfriendservice` 在本地运行
- Docker 只负责依赖服务，目前只有 `ZooKeeper`

这比纯 Docker-only 更接近真实 C++ 后端团队的开发方式，也更适合本地调试和面试展示。

## 当前项目依赖判断

当前这个 `mprpc` 项目真实依赖的外部服务只有：

- ZooKeeper

当前 **没有** 使用：

- MySQL
- Redis
- Nginx
- Kafka

所以 Docker Compose 只需要托管 ZooKeeper，不需要数据库初始化脚本或 Redis 配置。

## 快速开始

### 1. 宿主机安装一次性依赖

在 Ubuntu / WSL 下先安装：

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  pkg-config \
  libprotobuf-dev \
  protobuf-compiler \
  libzookeeper-mt-dev
```

Muduo 采用**系统全局安装优先**的方式，推荐安装到 `/usr/local`。

你需要保证本机可以找到：

- Muduo 头文件
- `libmuduo_net`
- `libmuduo_base`

也就是说，当前本地开发模式假定 Muduo 已经全局装好。

### 2. 启动依赖服务（ZooKeeper）

```bash
docker compose up -d zookeeper
docker compose ps
```

或者使用辅助脚本：

```bash
./scripts/deps-up.sh
docker compose ps
```

预期：

- `mprpc-zookeeper` 为 `Up`
- 宿主机可通过 `127.0.0.1:2181` 访问 ZooKeeper

### 3. 本地构建

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

或者使用辅助脚本：

```bash
./scripts/build-local.sh
```

预期：

- 可执行文件输出到 `build/bin/`
- 静态库输出到 `build/lib/libmprpc.a`
- 不再把产物默认输出到项目根目录 `bin/`、`lib/`

### 4. 本地运行服务端

开两个终端分别执行：

```bash
./build/bin/userservice -i config/local/userservice.conf
```

```bash
./build/bin/friendservice -i config/local/friendservice.conf
```

预期：

- `userservice` 监听 `127.0.0.1:8000`
- `friendservice` 监听 `127.0.0.1:8001`
- 启动日志里出现：
  - `zkclient start success`
  - `register rpc provider node`
  - `rpc provider start bind=127.0.0.1:...`

### 5. 本地运行客户端测试

```bash
./build/bin/calluserservice -i config/local/client.conf
```

预期输出：

```text
rpc login response success：1
rpc register response success:1
```

```bash
./build/bin/callfriendservice -i config/local/client.conf
```

预期输出：

```text
rpc GetFriendList success !
userid:1 name:zhang san
userid:2 name:li si
userid:3 name:wang wu
```

### 6. 停止依赖服务

```bash
docker compose down -v --remove-orphans
```

或者使用辅助脚本：

```bash
./scripts/deps-down.sh
```

## 为什么模式 A 更适合本地调试

这个项目现在的推荐方式不是 Docker-first，而是：

- 本地写代码
- 本地编译
- 本地运行服务端和客户端
- Docker 只负责依赖服务

这样做的好处是：

- 更容易接入 `gdb`、`lldb`、VS Code C++ 调试器
- 不需要每次改代码都重新进容器
- 更贴近真实团队里的 C++ 后端开发体验
- 更适合你以后给面试官展示“本地如何调试 RPC 框架”

## 配置文件

当前本地运行的主入口是配置文件，而不是 Docker 环境变量。

配置文件位于：

```text
config/local/userservice.conf
config/local/friendservice.conf
config/local/client.conf
```

### userservice.conf

- `RPC_BIND_IP=127.0.0.1`
- `RPC_ADVERTISE_HOST=127.0.0.1`
- `RPC_PORT=8000`
- `ZK_ENDPOINTS=127.0.0.1:2181`

### friendservice.conf

- `RPC_BIND_IP=127.0.0.1`
- `RPC_ADVERTISE_HOST=127.0.0.1`
- `RPC_PORT=8001`
- `ZK_ENDPOINTS=127.0.0.1:2181`

### client.conf

- 只保留 ZooKeeper 地址和超时配置
- 不需要服务监听地址

当前框架仍然支持：

```bash
-i <configfile>
```

并且这次它就是本地开发主路径。

## 本地开发命令清单

最短常用流程：

```bash
docker compose up -d zookeeper
cmake -S . -B build
cmake --build build -j"$(nproc)"
./build/bin/userservice -i config/local/userservice.conf
./build/bin/friendservice -i config/local/friendservice.conf
./build/bin/calluserservice -i config/local/client.conf
./build/bin/callfriendservice -i config/local/client.conf
docker compose down -v --remove-orphans
```

## 日志怎么看

本地运行时，终端里的输出一般分 3 类：

1. Docker Compose 输出

例如：

```text
Container mprpc-zookeeper Running
```

这只是依赖服务状态。

2. ZooKeeper C 客户端日志

例如：

```text
ZOO_INFO@zookeeper_init_internal...
```

这部分是 ZooKeeper C 库自己的日志。

3. 真正的业务验证结果

例如：

```text
rpc login response success：1
rpc register response success:1
rpc GetFriendList success !
userid:1 name:zhang san
userid:2 name:li si
userid:3 name:wang wu
```

如果你是为了验证项目是否跑通，最重要的就是这几行。

## 多实例说明

框架代码当前仍然支持多 provider 注册发现，ZooKeeper 节点结构是：

```text
/<namespace>/<service>/<method>/providers/provider-*
```

但在本地开发模式下，默认运行方式是：

- 一个 `userservice`
- 一个 `friendservice`

这样更利于调试和理解。

如果以后你想重新做多实例验证，可以额外起多个本地服务进程，分别使用不同端口配置。

## 仓库中的 Docker 角色

当前仓库里 Docker 的职责只有一个：

- 运行 ZooKeeper

它不再承担：

- 编译应用
- 运行服务端程序
- 运行客户端程序
- 运行冒烟测试主流程

## 目录说明

- `src/`
  - RPC 框架核心实现
- `example/`
  - 示例服务和客户端
- `config/local/`
  - 本地运行配置
- `scripts/build-local.sh`
  - 本地构建脚本
- `scripts/deps-up.sh`
  - 启动 ZooKeeper 依赖
- `scripts/deps-down.sh`
  - 停止 ZooKeeper 依赖
- `compose.yaml`
  - 仅用于依赖服务编排

## 测试方案

### 依赖层

```bash
docker compose up -d zookeeper
docker compose ps
```

检查：

- `mprpc-zookeeper` 为 `Up`

### 本地构建层

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

检查：

- `build/bin/userservice`
- `build/bin/friendservice`
- `build/bin/calluserservice`
- `build/bin/callfriendservice`
- `build/lib/libmprpc.a`

### 本地运行层

先启动本地服务端，再运行客户端。

检查：

- `calluserservice` 返回成功
- `callfriendservice` 返回成功
- 服务端日志中出现 ZooKeeper 注册成功信息
