# mprpc

`mprpc` 是一个基于 `C++14 + Protobuf + ZooKeeper + muduo` 实现的轻量级 RPC 框架，本仓库对外提供的核心产物是动态库 `libmprpc.so`。

这个项目来自我跟练施磊老师《C++ 项目 - 实现分布式网络通信框架 / RPC 通信原理》课程后的整理版本。目前仓库中已经包含：

- 框架动态库源码
- 对外头文件
- 服务提供者 / 服务消费者示例
- 配置文件与基础测试代码

## 项目定位

这个仓库不是单纯的 demo 程序集合，而是一个可以被外部项目接入的 RPC 通信库。

编译完成后，核心产物包括：

- `lib/libmprpc.so`：框架动态库
- `src/include/`：框架对外头文件

如果使用 `./autobuild.sh`，脚本还会额外把头文件拷贝到：

- `lib/include/`

这意味着外部项目可以把 `libmprpc.so + include/` 当作一个简单 SDK 来使用。

## 目录结构

```text
.
├── bin/                    # 配置文件与示例程序输出
├── build/                  # CMake 构建目录
├── example/                # 示例服务
│   ├── callee/             # 服务提供者示例
│   ├── caller/             # 服务消费者示例
│   ├── *.proto             # 业务 proto
│   └── *.pb.cc / *.pb.h    # 生成代码
├── lib/                    # 动态库输出目录
├── src/
│   ├── include/            # 框架对外头文件
│   ├── *.cc                # 框架源码
│   └── rpcherder.proto     # RPC 通信头 proto
├── test/
├── CMakeLists.txt
└── autobuild.sh
```

## 核心模块

- `MprpcApplication`：框架初始化入口，负责解析配置文件
- `MprpcConfig`：配置项读取
- `MprpcChannel`：消费者侧 RPC 通道，负责序列化、网络发送、接收响应
- `MprpcController`：记录一次 RPC 调用的错误状态
- `RpcProvider`：提供者侧服务发布对象
- `ZkClient`：服务注册与发现
- `Logger`：异步日志组件

## 依赖环境

建议在 Linux 环境下编译运行，并提前安装以下依赖：

- `g++` / `gcc`，支持 `C++14`
- `cmake >= 3.16`
- `protobuf` 与 `protoc`
- ZooKeeper C 客户端库，例如 `zookeeper_mt`
- `mymuduo` 网络库

说明：

- 当前工程通过 `find_package(Protobuf REQUIRED)` 查找 Protobuf。
- `mymuduo` 以库名方式链接，并支持从项目根目录下的 `lib/` 搜索。
- ZooKeeper 服务需要预先启动，否则服务注册与发现无法工作。

## 编译

### 方式一：直接使用 CMake

```bash
cmake -S . -B build
cmake --build build -j
```

### 方式二：使用脚本打包

```bash
chmod +x ./autobuild.sh
./autobuild.sh
```

编译后默认产物：

- `lib/libmprpc.so`
- `bin/provider`
- `bin/consumer`
- `bin/user_provider`
- `bin/user_consumer`

## 配置文件

示例配置文件位于 `bin/test.conf`：

```ini
rpcserverip=127.0.0.1
rpcserverport=8000
zookeeperip=127.0.0.1
zookeeperport=2181
```

程序启动时通过下面的形式加载配置：

```bash
./your_program -i ./bin/test.conf
```

## 对外使用方式

### 1. 服务提供者

服务提供者侧需要包含：

```cpp
#include "mprpcapplication.h"
#include "mprpcprovider.h"
```

这里需要特别说明：

- 项目里真实存在的头文件名是 `mprpcprovider.h`
- 不是 `rpcprovider.h`

一个最小使用方式如下：

```cpp
#include "mprpcapplication.h"
#include "mprpcprovider.h"
#include "user.pb.h"

class UserService : public fixbug::UserServiceRpc {
public:
  void Login(::google::protobuf::RpcController *controller,
             const ::fixbug::LoginRequest *request,
             ::fixbug::LoginResponse *response,
             ::google::protobuf::Closure *done) override {
    response->mutable_result()->set_errcode(0);
    response->mutable_result()->set_errormasg("");
    response->set_sucess(true);
    done->Run();
  }
};

int main(int argc, char **argv) {
  MprpcApplication::Init(argc, argv);

  RpcProvider provider;
  provider.NotifyService(new UserService());
  provider.Run();

  return 0;
}
```

### 2. 服务消费者

服务消费者侧需要包含：

```cpp
#include "mprpcapplication.h"
#include "mprpcchannel.h"
#include "mprpccontroller.h"
```

这里也有一个实际使用约束：

- 当前实现里，`MprpcChannel::CallMethod` 内部对 `controller` 做了非空断言
- 所以调用 RPC 时，`controller` 不是“可选增强项”，而是建议始终传入

一个最小使用方式如下：

```cpp
#include "mprpcapplication.h"
#include "mprpcchannel.h"
#include "mprpccontroller.h"
#include "user.pb.h"
#include <iostream>

int main(int argc, char **argv) {
  MprpcApplication::Init(argc, argv);

  fixbug::UserServiceRpc_Stub stub(new MprpcChannel());

  fixbug::LoginRequest request;
  request.set_name("zhang san");
  request.set_pwd("123456");

  fixbug::LoginResponse response;
  MprpcController controller;

  stub.Login(&controller, &request, &response, nullptr);

  if (controller.Failed()) {
    std::cout << controller.ErrorText() << std::endl;
    return 1;
  }

  std::cout << response.sucess() << std::endl;
  return 0;
}
```

## 外部项目如何链接

如果你把本项目当作动态库接入自己的工程，至少需要这两部分：

- 头文件目录：`src/include/` 或 `lib/include/`
- 动态库文件：`lib/libmprpc.so`

以 CMake 项目为例，可以参考：

```cmake
include_directories(/path/to/03_rpc_framework/src/include)
link_directories(/path/to/03_rpc_framework/lib)

add_executable(your_app main.cc user.pb.cc)
target_link_libraries(your_app PRIVATE mprpc protobuf pthread)
```

如果运行时找不到 `libmprpc.so`，需要把动态库目录加入运行时搜索路径，例如：

```bash
export LD_LIBRARY_PATH=/path/to/03_rpc_framework/lib:$LD_LIBRARY_PATH
```

## RPC 调用流程

1. 服务提供者通过 `RpcProvider::NotifyService` 注册服务对象。
2. `RpcProvider::Run` 启动网络服务，并将服务信息注册到 ZooKeeper。
3. 消费者通过 Protobuf 生成的 `Stub` 发起远程调用。
4. `MprpcChannel` 负责把方法名、服务名和参数打包发送给远端。
5. 提供者解析请求后调用本地业务方法。
6. 业务结果序列化后返回给消费者。

## 示例程序

仓库中包含两组示例：

- `bin/provider` / `bin/consumer`：好友服务
- `bin/user_provider` / `bin/user_consumer`：用户服务

运行前请先确保 ZooKeeper 已启动，然后执行：

```bash
./bin/provider -i ./bin/test.conf
./bin/consumer -i ./bin/test.conf
```

或者：

```bash
./bin/user_provider -i ./bin/test.conf
./bin/user_consumer -i ./bin/test.conf
```

## 当前 CMake 调整

目前已经做过一轮整理，主要包括：

- 去掉全局 `include_directories` / `link_directories` 的滥用
- 使用 target 级别的 include 和 link 配置
- 去掉 `aux_source_directory`，改为显式源码列表
- 增加 `Threads`、`Protobuf` 的标准查找方式
- 增加 `MPRPC_BUILD_EXAMPLES` 选项
- 把 `UserService` 和 `FriendService` 两组示例都接入构建流程

## 说明

这个项目很适合用来理解下面这些 RPC 核心机制：

- 服务注册与发现
- 请求和响应序列化
- 网络通信与方法分发
- 本地服务与远程调用之间的桥接
