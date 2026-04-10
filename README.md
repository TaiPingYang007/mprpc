# mprpc

一个基于 `C++ + Protobuf + ZooKeeper + muduo` 实现的轻量级 RPC 框架练手项目。

这个项目主要用来系统梳理 RPC 框架里的几个核心问题：服务发布与发现、请求编解码、网络通信、同步调用链路、配置加载，以及如何把这些模块组织成一个能独立复用的框架库。它的定位不是“只演示一次调用的 demo”，而是一个可以被外部服务接入的静态库项目。

当前仓库的核心产物是：

- `lib/libmprpc.a`
- `src/include/` 下的对外头文件
- `bin/provider` / `bin/consumer`
- `bin/user_provider` / `bin/user_consumer`

之所以当前生成的是静态库而不是共享库，是因为本机接入的 `muduo` 只有静态库版本，且这套静态库不是按 `-fPIC` 编译的，无法继续向上生成 `libmprpc.so`。如果后续把 `muduo` 替换成共享库，或者重新编成带 `-fPIC` 的静态库，再切回共享版 `mprpc` 即可。

## 项目亮点

- 基于 `Protobuf` 的自定义 RPC 协议封装，链路格式为：`header_size + rpc_header + args`
- 基于 `ZooKeeper` 做服务注册与服务发现，消费者通过 `service/method` 路径查找提供者地址
- 提供者侧网络层接入陈硕版 `muduo`，由 `RpcProvider` 负责监听、解包、分发和回包
- 消费者侧封装 `MprpcChannel`，把 `Stub` 调用转换为序列化、发包、收包和反序列化流程
- 框架入口、配置模块、控制器、日志模块相互解耦，便于面试时分模块讲解
- 仓库自带两组业务示例，适合直接演示一次完整 RPC 调用流程

## 通过项目可以看出的能力

- 熟悉 `C++` 网络编程基础，能把 Socket 通信封装成可复用的 RPC 调用链路
- 理解 `Protobuf` 在序列化、接口生成、服务抽象中的使用方式
- 理解 `ZooKeeper` 在服务注册、服务发现中的角色，以及节点路径设计思路
- 能把 `muduo` 接入到业务框架中，完成监听、收包、解包、回包流程
- 具备一定的协议设计能力，知道为什么要做请求头、长度字段和方法定位
- 具备基础工程化能力，能通过 `CMake`、静态库封装、示例程序组织完整项目结构
- 具备问题定位和兼容改造能力，能够处理第三方库替换、头文件调整和构建链修改

## 技术栈

- `C++14` 编译，手写代码按 `C++11` 风格整理
- `Protobuf`
- `ZooKeeper C Client`
- `muduo`
- `CMake`
- Linux Socket API

## 目录结构

```text
.
├── bin/                    # 配置文件与示例程序输出
├── build/                  # 本地构建目录
├── example/                # 示例服务与调用方
│   ├── callee/
│   ├── caller/
│   ├── friend.proto
│   ├── user.proto
│   └── *.pb.cc / *.pb.h
├── lib/                    # 静态库输出目录
├── src/
│   ├── include/            # 对外头文件
│   ├── *.cc                # 框架源码
│   └── rpcherder.proto     # RPC 请求头 proto
├── test/
├── CMakeLists.txt
└── autobuild.sh
```

## 核心模块

- `MprpcApplication`
  负责框架初始化与配置加载入口。
- `MprpcConfig`
  负责解析配置文件，读取 `rpcserverip`、`rpcserverport`、`zookeeperip`、`zookeeperport`。
- `MprpcChannel`
  消费者侧 RPC 通道，把本地 `Stub` 调用转成一次真实的远程网络请求。
- `MprpcController`
  记录一次 RPC 调用是否失败，以及失败原因。
- `RpcProvider`
  提供者侧网络对象，负责服务发布、网络监听、请求分发和响应回写。
- `ZkClient`
  封装 ZooKeeper 客户端连接、节点创建和节点查询逻辑。
- `Logger`
  简单的异步日志模块。

## 一次 RPC 调用是怎么跑通的

1. 服务提供者启动后调用 `RpcProvider::NotifyService` 注册业务服务。
2. `RpcProvider::Run` 创建 `muduo::net::TcpServer`，并把服务信息注册到 ZooKeeper。
3. 消费者侧通过 Protobuf 生成的 `Stub` 发起调用。
4. `MprpcChannel::CallMethod` 取出服务名、方法名和请求体，组装成自定义协议并发送。
5. 提供者收到请求后解析 `RpcHeader`，定位到对应的 `Service` 和 `Method`。
6. 业务方法执行完成后，通过回调把响应序列化回客户端。
7. 消费者收到响应后反序列化，最终拿到业务结果。

## 自定义协议说明

为了避免 TCP 粘包拆包时无法切分请求，当前协议把请求拆成三段：

```text
4 bytes header_size + rpc_header_str + args_str
```

其中：

- `header_size`：`rpc_header_str` 的长度
- `rpc_header_str`：由 `rpcherder.proto` 定义，内部包含：
  - `service_name`
  - `method_name`
  - `args_size`
- `args_str`：真实业务请求参数的 Protobuf 序列化结果

## 环境依赖

建议在 Linux 环境下编译，并提前准备好以下依赖：

- `g++` / `gcc`
- `cmake >= 3.16`
- `protobuf` 与 `protoc`
- `zookeeper_mt`
- `muduo`

当前工程默认按下面这个路径查找 `muduo`：

```bash
/home/taipingyang/thirdparty/muduo
```

这个目录下至少需要有：

- `include/muduo`
- `lib/libmuduo_net.a`
- `lib/libmuduo_base.a`

如果你的 `muduo` 不在这个位置，可以在 CMake 配置时手动指定：

```bash
cmake -S . -B build -DMUDUO_ROOT=/your/path/to/muduo
```

## 编译方式

### 方式一：直接用 CMake

```bash
cmake -S . -B build -DMUDUO_ROOT=/home/taipingyang/thirdparty/muduo
cmake --build build -j
```

编译完成后，主要产物包括：

- `lib/libmprpc.a`
- `bin/provider`
- `bin/consumer`
- `bin/user_provider`
- `bin/user_consumer`

### 方式二：使用脚本一键构建

```bash
chmod +x ./autobuild.sh
MUDUO_ROOT=/home/taipingyang/thirdparty/muduo ./autobuild.sh
```

脚本会：

- 清理 `build/`
- 重新执行 CMake 与编译
- 把 `src/include/` 拷贝到 `lib/include/`

## 配置文件

示例配置文件在 [bin/test.conf](/home/taipingyang/learn/cpp_project/03_rpc_framework/bin/test.conf)：

```ini
rpcserverip=127.0.0.1
rpcserverport=8000
zookeeperip=127.0.0.1
zookeeperport=2181
```

程序启动时通过下面的方式加载：

```bash
./your_program -i ./bin/test.conf
```

## 如何运行示例

### 1. 先启动 ZooKeeper

当前项目依赖 ZooKeeper 做服务注册与发现。如果 ZooKeeper 没启动，提供者和消费者都无法正常工作。

### 2. 启动提供者

好友服务示例：

```bash
./bin/provider -i ./bin/test.conf
```

用户服务示例：

```bash
./bin/user_provider -i ./bin/test.conf
```

### 3. 再启动消费者

好友服务消费者：

```bash
./bin/consumer -i ./bin/test.conf
```

用户服务消费者：

```bash
./bin/user_consumer -i ./bin/test.conf
```

## 外部项目如何接入

如果把本项目当成一个静态库来接入自己的工程，至少需要以下内容：

- 头文件目录：`src/include/` 或 `lib/include/`
- 静态库文件：`lib/libmprpc.a`
- `muduo`、`protobuf`、`zookeeper_mt`、`pthread` 等依赖

一个最小 CMake 接入示意如下：

```cmake
include_directories(/path/to/03_rpc_framework/src/include)
include_directories(/path/to/muduo/include)

add_executable(your_app main.cc user.pb.cc)

target_link_libraries(
  your_app
  PRIVATE
    /path/to/03_rpc_framework/lib/libmprpc.a
    /path/to/muduo/lib/libmuduo_net.a
    /path/to/muduo/lib/libmuduo_base.a
    protobuf
    zookeeper_mt
    pthread)
```

## 建议重点关注的实现点

- 自定义 RPC 协议的设计与拆包思路
- `Stub -> RpcChannel -> ZooKeeper -> Provider` 的完整调用链
- `RpcProvider` 如何根据 `service_name` 和 `method_name` 分发请求
- `muduo` 在提供者侧如何承接网络事件和消息回调
- 当前静态库构建方式与第三方依赖的关系

## 当前版本的已知限制

- 当前产物是 `libmprpc.a`，不是共享库。
- 客户端调用链路是同步阻塞模型，暂未提供超时控制。
- `ZkClient` 每次调用都会重新发起连接，性能上还有优化空间。
- `RpcController` 的取消语义当前没有真正实现。
- 端到端运行依赖本地 ZooKeeper 环境，示例程序默认使用固定配置文件。

## 后续优化方向

- 把 `muduo` 依赖升级为共享库，恢复 `libmprpc.so` 产物。
- 给 `MprpcChannel` 增加连接超时、读写超时和更细粒度的错误分类。
- 引入连接复用或连接池，降低每次 RPC 的建立连接成本。
- 优化 `RpcProvider` 的多请求处理能力和异常输入保护。
- 完善单元测试、集成测试和压测脚本。
