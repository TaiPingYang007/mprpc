# mprpc

基于 `C++14 + Protobuf + ZooKeeper + muduo` 实现的轻量级 RPC 通信框架练手项目，用来演示一个 RPC 调用从“本地桩对象发起请求”到“服务节点执行并返回结果”的完整链路。

这个仓库是我跟练施磊老师《C++ 项目 - 实现分布式网络通信框架 / RPC 通信原理》课程时整理出的代码实现，当前已经包含框架核心模块、服务发布端/调用端示例，以及基础的 Protobuf 测试代码。

## 项目特性

- 基于 `Protobuf` 描述服务接口、请求和响应数据结构
- 自定义 `RpcChannel` / `RpcController`，完成请求序列化、网络发送和错误上报
- 基于 `muduo` 风格网络库封装服务发布端，支持远程方法调用
- 使用 `ZooKeeper` 做服务注册与发现，客户端通过服务名/方法名定位节点
- 提供异步日志模块，方便观察框架运行过程
- 附带 `FriendService` 和 `UserService` 两组示例服务

## 目录结构

```text
.
├── bin/                    # 配置文件与编译输出
├── example/                # RPC 调用示例
│   ├── caller/             # 服务消费者
│   ├── callee/             # 服务提供者
│   ├── *.proto             # 业务 proto 定义
│   └── *.pb.cc / *.pb.h    # 生成代码
├── src/
│   ├── include/            # 框架对外头文件
│   ├── *.cc                # 框架实现
│   └── rpcherder.proto     # RPC 通信头定义
├── test/protobuf/          # Protobuf 基础测试代码
├── CMakeLists.txt
└── autobuild.sh
```

## 核心模块

- `MprpcApplication`：框架初始化入口，负责读取配置文件
- `MprpcConfig`：解析 `rpcserverip`、`rpcserverport`、`zookeeperip` 等配置
- `MprpcChannel`：客户端发起远程调用时的核心通道，完成序列化、寻址、发送和接收
- `MprpcController`：保存一次 RPC 调用过程中的状态和错误信息
- `RpcProvider`：服务发布端，负责注册服务、启动网络监听、反序列化请求并回调本地业务
- `ZkClient`：ZooKeeper 客户端封装，负责服务注册与发现
- `Logger`：简单异步日志组件

## RPC 调用流程

1. 服务提供者启动后，通过 `RpcProvider::NotifyService` 注册本地服务对象。
2. `RpcProvider::Run` 启动网络服务，并把服务名、方法名注册到 ZooKeeper。
3. 调用方通过 Protobuf 生成的 `Stub` 发起调用。
4. `MprpcChannel` 将 `service_name`、`method_name`、参数长度、参数体打包发送给服务端。
5. 服务端解析消息头，根据服务名和方法名定位本地方法并执行。
6. 执行结果序列化后返回客户端，客户端再反序列化成响应对象。

## 环境依赖

建议在 Linux 环境下编译运行，并提前准备好以下依赖：

- `g++` / `gcc`，支持 `C++14`
- `cmake >= 3.16`
- `protobuf` 与 `protoc`
- `zookeeper` C 客户端库（如 `zookeeper_mt`）
- `mymuduo` 网络库

说明：

- 当前 CMake 会通过 `find_package(Protobuf REQUIRED)` 查找 Protobuf。
- `mymuduo` 默认按系统库名链接，同时也会查找项目根目录下的 `lib/`。
- ZooKeeper 服务需要提前启动，否则服务注册和发现无法完成。

## 构建方式

### 方式一：使用 CMake

```bash
cmake -S . -B build
cmake --build build -j
```

### 方式二：使用脚本

```bash
chmod +x ./autobuild.sh
./autobuild.sh
```

默认会生成这些可执行文件：

- `bin/provider`：`FriendService` 服务提供者
- `bin/consumer`：`FriendService` 服务消费者
- `bin/user_provider`：`UserService` 服务提供者
- `bin/user_consumer`：`UserService` 服务消费者

## 配置文件

示例配置文件位于 `bin/test.conf`：

```ini
rpcserverip=127.0.0.1
rpcserverport=8000
zookeeperip=127.0.0.1
zookeeperport=2181
```

如果服务端和客户端不在同一台机器上，请按实际部署环境调整 IP 和端口。

## 运行示例

### 1. 启动 ZooKeeper

请先确保本地 ZooKeeper 服务已经正常运行。

### 2. 启动服务提供者

好友服务示例：

```bash
./bin/provider -i ./bin/test.conf
```

用户服务示例：

```bash
./bin/user_provider -i ./bin/test.conf
```

### 3. 启动服务消费者

好友服务调用：

```bash
./bin/consumer -i ./bin/test.conf
```

用户服务调用：

```bash
./bin/user_consumer -i ./bin/test.conf
```

## CMake 优化说明

这次整理里顺手把 CMake 做了几项更适合长期维护的优化：

- 去掉了全局 `include_directories` / `link_directories` 的滥用，改成 target 级配置
- 去掉 `aux_source_directory`，改成显式列出框架源码，避免误编译和漏编译
- 增加 `Threads`、`Protobuf` 的标准查找方式
- 增加 `MPRPC_BUILD_EXAMPLES` 选项，便于按需关闭示例编译
- 补齐 `ARCHIVE/LIBRARY/RUNTIME` 输出目录，统一产物位置
- 把两个示例服务都接入到 CMake 构建流程中

## 后续可继续完善的方向

- 自动生成 `.proto` 对应的 `.pb.cc/.pb.h`，减少手工维护
- 为框架补充单元测试和集成测试
- 增加安装规则，支持 `make install` / SDK 打包
- 改进日志、超时控制、重试策略和异常处理

## 说明

这个项目的重点在于理解 RPC 框架的核心原理，包括：

- 服务注册与发现
- 请求和响应的序列化
- 远程调用过程中的网络通信
- 本地方法与远程方法之间的桥接

如果你也在学习 RPC、Protobuf、ZooKeeper 和网络编程，这个仓库很适合作为一个入门级实践项目。
