# mprpc

一个基于 `C++11 + Muduo + Protobuf + ZooKeeper` 的轻量级 RPC 框架练手项目。

现在这份仓库已经改成 **Docker-first** 用法：

- 标准构建方式是 `docker compose build`
- 标准启动方式是 `docker compose up`
- 标准测试方式是 `docker compose run --rm ...`
- 不再要求先在宿主机手工安装 Muduo、Protobuf、ZooKeeper C Client

## 超短照抄版

如果你以后只想照着跑这一套，直接按下面执行：

```bash
cp .env.example .env
docker compose build
docker compose up -d zookeeper userservice-1 userservice-2 friendservice-1
docker compose ps
docker compose run --rm client calluserservice
docker compose run --rm client callfriendservice
docker compose run --rm smoke-test
docker compose down -v --remove-orphans
```

对应成功信号：

`docker compose ps`

- `zookeeper` 为 `Up`
- `userservice-1`、`userservice-2`、`friendservice-1` 为 `Up`
- 三个业务服务最好显示为 `healthy`

`docker compose run --rm client calluserservice`

```text
rpc login response success：1
rpc register response success:1
```

`docker compose run --rm client callfriendservice`

```text
rpc GetFriendList success !
userid:1 name:zhang san
userid:2 name:li si
userid:3 name:wang wu
```

`docker compose run --rm smoke-test`

- 会顺序跑一遍用户服务和好友服务调用
- 正常退出即可
- 如果想明确确认退出码，可以执行 `echo $?`，预期为 `0`

## 快速开始

### 1. 准备环境变量

```bash
cp .env.example .env
```

作用：
- 复制项目默认环境变量
- `docker compose` 会自动读取 `.env`

### 2. 构建镜像

```bash
docker compose build
```

作用：
- 在 Docker 镜像内安装编译依赖
- 在 Docker 镜像内拉取并编译 Muduo
- 在 Docker 镜像内编译当前项目

### 3. 启动基础服务和 RPC 服务

```bash
docker compose up -d zookeeper userservice-1 userservice-2 friendservice-1
docker compose ps
```

预期：
- `zookeeper` 为 `Up`
- `userservice-1`、`userservice-2`、`friendservice-1` 为 `Up`
- 三个业务服务最好显示为 `healthy`

### 4. 运行客户端测试

```bash
docker compose run --rm client calluserservice
docker compose run --rm client callfriendservice
docker compose run --rm smoke-test
```

预期：

`calluserservice`

```text
rpc login response success：1
rpc register response success:1
```

`callfriendservice`

```text
rpc GetFriendList success !
userid:1 name:zhang san
userid:2 name:li si
userid:3 name:wang wu
```

`smoke-test`

- 会顺序执行用户服务和好友服务调用
- 正常情况下命令退出码为 `0`

### 5. 停止环境

```bash
docker compose down -v --remove-orphans
```

作用：
- 停止容器
- 删除 compose 网络和匿名卷
- 清理本次运行环境

## 最短命令清单

如果你以后只想照抄一版最短流程，用这一组：

```bash
cp .env.example .env
docker compose build
docker compose up -d zookeeper userservice-1 userservice-2 friendservice-1
docker compose run --rm client calluserservice
docker compose run --rm client callfriendservice
docker compose run --rm smoke-test
docker compose down -v --remove-orphans
```

## 服务说明

当前 `compose.yaml` 会编排这些服务：

- `zookeeper`
  - 服务注册与发现中心
- `userservice-1`
  - 第一个用户服务实例
- `userservice-2`
  - 第二个用户服务实例
- `friendservice-1`
  - 好友服务实例
- `client`
  - 临时客户端容器，用于手动调用 RPC
- `smoke-test`
  - 一键冒烟测试容器

所有 RPC 服务都在 Docker 网络内监听 `8000`，客户端通过服务名访问，不使用 `127.0.0.1`。

## 多实例与服务发现

这个项目现在支持 `userservice` 多实例注册和发现。

ZooKeeper 中的注册结构是：

```text
/<namespace>/<service>/<method>/providers/provider-*
```

例如：

```text
/mprpc/UserServiceRpc/Login/providers/provider-0000000000
/mprpc/UserServiceRpc/Login/providers/provider-0000000001
```

节点值保存的是：

```text
userservice-1:8000
userservice-2:8000
```

客户端调用时会：

- 读取某个方法下所有 provider 子节点
- 按节点名排序
- 做本地轮询选择

## 轮询验证

可以用下面这条命令在同一个客户端进程中连续发起 4 次 `Login`：

```bash
docker compose run --rm -e MPRPC_LOGIN_REPEAT=4 client calluserservice
```

预期输出类似：

```text
rpc login response success[1/4]：1
rpc login response success[2/4]：1
rpc login response success[3/4]：1
rpc login response success[4/4]：1
rpc register response success:1
```

然后查看两个实例的日志：

```bash
docker compose logs --tail=50 userservice-1
docker compose logs --tail=50 userservice-2
```

预期：
- 两边都能看到 `UserServiceRpc` 请求日志
- 说明两个实例都被命中了

## 日志与排障

查看最近日志：

```bash
docker compose logs --tail=100 zookeeper userservice-1 userservice-2 friendservice-1
```

实时跟日志：

```bash
docker compose logs -f userservice-1
docker compose logs -f userservice-2
docker compose logs -f friendservice-1
```

正常启动时你通常会看到这些关键信息：

- `zkclient start success! endpoints=zookeeper:2181`
- `register rpc provider node ...`
- `rpc provider start bind=0.0.0.0:8000 advertise=...`

### 为什么日志里会看到很多 `newConnection/removeConnection`

这是正常现象，主要来自 `docker compose` 的健康检查：

```yaml
healthcheck:
  test: ["CMD-SHELL", "nc -z 127.0.0.1 8000"]
```

它会定时探测端口是否存活，所以服务端日志里会不断出现：

- `TcpServer::newConnection`
- `TcpServer::removeConnectionInLoop`

这不代表业务调用异常，只代表健康检查正在工作。

## 配置优先级

运行配置优先级固定为：

```text
环境变量 > 配置文件 > 默认值
```

当前主路径推荐直接使用：

- `.env`
- `compose.yaml`

兼容配置文件保留在：

```text
config/mprpc.example.conf
```

框架仍然兼容旧的：

```bash
-i <configfile>
```

但它现在只是兼容入口，不再是推荐主路径。

## 常用环境变量

- `MUDUO_REF`
  - Docker 构建时使用的 Muduo commit
- `MPRPC_ZK_NAMESPACE`
  - ZooKeeper 根命名空间，默认 `mprpc`
- `ZK_ENDPOINTS`
  - ZooKeeper 地址，默认 `zookeeper:2181`
- `RPC_PORT`
  - 服务端监听端口，默认 `8000`
- `RPC_IO_THREADS`
  - Muduo 线程数，默认 `4`
- `RPC_BIND_IP`
  - 服务监听地址，默认 `0.0.0.0`
- `RPC_ADVERTISE_HOST`
  - 注册到 ZooKeeper 的服务地址
- `RPC_CONNECT_TIMEOUT_MS`
  - 客户端连接超时
- `RPC_SEND_TIMEOUT_MS`
  - 客户端发送超时
- `RPC_RECV_TIMEOUT_MS`
  - 客户端接收超时
- `MPRPC_LOG_MODE`
  - 日志模式，默认 `stdout`
- `MPRPC_LOG_DIR`
  - 文件日志目录，仅 `file` 模式使用

## 目录说明

- `Dockerfile`
  - 多阶段构建，负责编译 Muduo 和当前项目
- `compose.yaml`
  - 服务编排入口
- `.env.example`
  - 环境变量模板
- `config/mprpc.example.conf`
  - 兼容旧配置文件模式的示例配置
- `docker/mprpc-entrypoint.sh`
  - 容器入口脚本

## 当前范围

这个仓库当前只包含这些运行依赖：

- ZooKeeper
- RPC 服务端
- RPC 客户端

当前 **没有** 接入：

- MySQL
- Redis
- Nginx

所以本仓库也没有数据库初始化脚本或 Redis 配置文件。

## 本地兼容说明

虽然框架仍然可以走本地二进制方式运行，但那不是推荐主流程。

推荐方式始终是：

```bash
docker compose build
docker compose up
docker compose run --rm ...
```

如果以后你要把这个仓库当模板复用，优先沿用这套 Docker-first 结构。
