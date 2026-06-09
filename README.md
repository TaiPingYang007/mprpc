# mprpc — 轻量级分布式 RPC 框架

基于 **C++11 + Protobuf + Muduo + ZooKeeper** 实现的轻量级 RPC 框架:用 Protobuf 描述服务、Muduo 承载高并发网络、ZooKeeper 做服务注册与发现,并内置一套生产级的异步日志子系统。框架以静态库形式打包,可被其他后端项目直接复用。

> 技术栈:`C++11` · `Protocol Buffers` · `Muduo` · `ZooKeeper` · `CMake` · `Docker`

## ✨ 特性亮点

**RPC 核心**
- 基于 Protobuf `Service / Stub` 的远程调用模型,业务侧像调用本地方法一样发起 RPC。
- 自定义二进制帧协议 `[4 字节 headerSize][RpcHeader][args]`,`RpcHeader` 携带 service / method / args_size。
- Muduo 多 Reactor 网络模型承载并发连接,I/O 线程数可配置。

**服务治理**
- ZooKeeper 服务注册与发现:provider 启动时在 `/<namespace>/<service>/<method>/providers/` 下创建顺序临时节点,caller 运行时动态发现地址,支持多实例。
- `MprpcController` 区分**框架层失败**(连接 / 序列化 / 网络)与**业务层失败**(错误码 / 错误信息)。

**生产级异步日志子系统(`RpcLogger`)— 本项目的工程重点**
- **业务线程零阻塞**:日志走生产者 / 消费者模型,业务线程只做一次入队,慢 I/O(写盘、flush)交给独立消费线程。
- **有界队列 + 丢最旧并计数**:洪峰下内存有界;被丢弃的日志数会被统计并以 `[RPC][WARN]` 行可视化,既不无声丢失,也绝不反压业务线程。
- **优雅停机**:`停止标志 + 排空队列 + flush + fclose + join`,进程退出前把尾部日志全部落盘,不丢尾巴;生命周期由调用方显式控制,不依赖静态析构顺序。
- **配置热路径优化**:消费线程通过 `原子版本号(epoch) + 互斥锁` 读取已锁定配置,每行仅一次无锁原子读,避免逐行查配置。
- **配置驱动**:输出模式(stdout / 按天滚动文件)、目录、日志文件名、队列容量均可由 `环境变量 > 配置文件 > 内置默认` 三级覆盖。
- **配套验收测试**:`不丢尾日志`、`有界丢弃` 两个可重复运行的验收用例。

**工程化**
- CMake 构建,`-Wall -Wextra` 干净;静态库 `libmprpc.a` 可通过 `cmake --install` 导出头文件 + 库,供其他后端项目直接接入。
- Docker 仅负责依赖服务(ZooKeeper)编排,代码本地编译 / 调试,贴近真实后端开发流程。

## 🏗 架构概览

```
Caller 进程                          ZooKeeper                     Provider 进程
Stub → MprpcChannel ──查询 provider──►   注册中心   ◄──注册节点──── RpcProvider → Service 实现
  │  序列化 + 组帧                          (仅服务发现)                   拆帧 + 分发 │
  └────────── TCP: [headerSize][RpcHeader][args] ───────────────────────────────────┘
                          ◄────────── TCP: [response bytes] ──────────────────────────
```

- **Stub / MprpcChannel**(调用方):把本地调用序列化组帧、查 ZooKeeper 发现地址、TCP 收发。
- **RpcProvider**(提供方):发布服务、收包拆帧、按 service / method 分发到业务实现、回包。
- **ZooKeeper**:只做服务注册与发现,不转发业务请求。
- **RpcLogger**:贯穿两端的异步日志子系统。

> 单次调用的完整时序(组帧、服务发现、拆包、分发、回包)见 [docs/rpc-call-flow-notes.md](docs/rpc-call-flow-notes.md)。

## 🚀 构建与运行

### 0. 一次性依赖(Ubuntu / WSL)

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config \
  libprotobuf-dev protobuf-compiler libzookeeper-mt-dev
```

Muduo 推荐全局安装到 `/usr/local`(需能找到 `libmuduo_net` / `libmuduo_base` 及头文件)。

### 1. 启动依赖服务(ZooKeeper)

```bash
docker compose up -d zookeeper      # 或 ./scripts/deps-up.sh
```

### 2. 构建

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

产物:可执行文件在 `build/bin/`,静态库在 `build/lib/libmprpc.a`。

### 3. 运行服务端 + 客户端

```bash
# 终端 A:启动服务端(监听 127.0.0.1:9000,并注册到 ZooKeeper)
./build/bin/userservice -i config/local/userservice.conf

# 终端 B:发起 RPC 调用
./build/bin/calluserservice -i config/local/client.conf
```

预期输出:

```text
rpc login response success：1
rpc register response success:1
```

(同理:`friendservice`(127.0.0.1:8001)配 `callfriendservice`。)

### 4.(可选)导出静态库供其他项目复用

一步到位(构建 + 导出头文件和静态库到 `dist/mprpc/`):

```bash
./scripts/pack.sh
```

`pack.sh` 等价于「构建 + `cmake --install build --prefix dist/mprpc`」。导出后:头文件在 `dist/mprpc/include/`,静态库在 `dist/mprpc/lib/libmprpc.a`,其他项目(如 BridgeIM)据此接入。

## 🧪 测试

```bash
# 日志子系统验收测试(构建后)
./build/test/logger/log_shutdown_test   # 连续写 N 条后立即关闭,文件里完整 N 条,不丢尾巴
./build/test/logger/log_bounded_test    # 洪峰压满队列,内存有界、丢弃被计数
```

端到端验证即上文「运行服务端 + 客户端」:看到 `rpc ... response success` 即表示调用链路打通。

## 📂 目录结构

```
src/            RPC 框架核心(provider / channel / controller / config / zookeeper / logger)
src/include/    框架公开头文件
example/        示例服务(userservice / friendservice)与客户端(call*)
test/logger/    日志子系统验收测试
config/local/   本地运行配置(-i <conf>)
scripts/        构建与依赖编排脚本
docs/           设计与调用链路笔记
compose.yaml    依赖服务(ZooKeeper)编排
```

## ⚙️ 配置

运行通过 `-i <configfile>` 指定配置;所有键均支持环境变量覆盖,优先级 `env > 配置文件 > 内置默认`:

| 键 | 说明 |
| --- | --- |
| `RPC_BIND_IP` / `RPC_PORT` | 服务监听地址 |
| `RPC_ADVERTISE_HOST` | 注册到 ZooKeeper 的对外地址 |
| `ZK_ENDPOINTS` | ZooKeeper 地址 |
| `RPC_IO_THREADS` | Muduo I/O 线程数 |
| `MPRPC_LOG_MODE` | 日志输出:`stdout` / `file` |
| `MPRPC_LOG_DIR` | 文件模式日志目录(按天滚动) |
| `MPRPC_LOG_NAME` | file 模式下日志文件名中的服务标识，默认 `mprpc`；建议使用不含路径分隔符的文件名 stem |
| `MPRPC_LOG_QUEUE_CAP` | 日志队列容量上限(满则丢最旧并计数) |

file 模式下日志文件格式为：`<MPRPC_LOG_DIR>/<YYYY-MM-DD>-<MPRPC_LOG_NAME>.log`。例如 `MPRPC_LOG_DIR=logs`、`MPRPC_LOG_NAME=friend_service-rpc` 会写入 `logs/2026-06-08-friend_service-rpc.log`；未配置时默认写入 `logs/2026-06-08-mprpc.log`。
