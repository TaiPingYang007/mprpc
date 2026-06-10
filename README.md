# mprpc · 可复用的 C++ RPC 框架

> 用 Protobuf 描述服务、Muduo 承载并发网络、ZooKeeper 做服务发现,
> 编译成静态库 `libmprpc.a` 供其他 C++ 后端项目直接接入 RPC 能力。

![C++11](https://img.shields.io/badge/C%2B%2B-11-00599C?logo=cplusplus&logoColor=white)
![Protobuf](https://img.shields.io/badge/Protocol%20Buffers-3.x-4285F4?logo=google&logoColor=white)
![Muduo](https://img.shields.io/badge/Muduo-multi--Reactor-2E7D32)
![ZooKeeper](https://img.shields.io/badge/ZooKeeper-service%20discovery-D32F2F)
![CMake](https://img.shields.io/badge/CMake-3.10%2B-064F8C?logo=cmake&logoColor=white)
![Build](https://img.shields.io/badge/build-Wall%20Wextra%20clean-success)

---

## 项目概览 · Overview

`mprpc` 是一个**可被其他项目复用的 RPC 框架**:业务方用 `.proto` 定义服务,
框架负责把"本地方法调用"变成跨进程的网络调用——序列化、组帧、服务发现、
收发、拆帧、分发回业务实现,全部封装在静态库里。配套的两个示例服务
(`UserService` / `FriendService`)演示了"提供方发布服务、调用方像调本地函数
一样发起 RPC"的完整闭环。

**技术深度集中在三处**,后文每一处都配有可点击的代码锚点:

1. **并发网络模型** — 服务端用 Muduo 多 Reactor 承载并发连接;
   调用方手写了带超时的非阻塞 `connect` 与完整收发,贴近 socket 底层。
2. **协议与服务边界设计** — 自定义二进制帧解决 TCP 粘包,复用 Protobuf
   `Service/Stub` 调用模型,并用 `MprpcController` 把"框架层失败"与
   "业务层失败"清晰分离。
3. **异步日志子系统** — 生产者/消费者模型让业务线程零阻塞;有界队列
   "丢最旧 + 计数"在洪峰下保证内存有界;优雅停机保证退出前尾部日志不丢。

> 服务注册与发现交给 ZooKeeper:提供方启动时注册地址,调用方运行时发现地址,
> ZooKeeper 只做注册中心、不转发业务流量。

## 核心亮点 · Highlights

### 调用方手写带超时的非阻塞网络收发

不依赖第三方客户端库,直接基于 socket 实现了**带超时的非阻塞 `connect`**:
切到 `O_NONBLOCK` 后用 `poll` 等待可写,再用 `getsockopt(SO_ERROR)` 确认连接
结果,完整处理了 `EINPROGRESS` / `EINTR` / `ETIMEDOUT`。发送侧用循环
`SendAll` 应对短写,地址解析走 `getaddrinfo` 兼容主机名/IPv4/IPv6。

> 深度在:把"连接超时控制"这件容易被一句 `connect()` 糊弄过去的事做对了。
> 锚点 — [`ConnectWithTimeout`](src/mprpcchannel.cc#L78) ·
> [`SendAll`](src/mprpcchannel.cc#L136) ·
> [`ResolveEndpoint`](src/mprpcchannel.cc#L173)

### 服务端 Muduo 多 Reactor + 自定义帧拆包

服务端组合 Muduo `TcpServer`,I/O 线程数可配置(`RPC_IO_THREADS`),由多个
Reactor 承载并发连接。`onMessage` 里按 `[4字节 headerSize][RpcHeader][args]`
的帧格式拆包,并在数据不足一个完整帧时直接返回、等待下次回调——正确处理了
TCP 粘包/半包。

> 深度在:网络框架的事件回调与"应用层定帧"的边界处理。
> 锚点 — [`RpcProvider::onMessage`](src/mprpcprovider.cc#L207) ·
> [`ExecuteRpcRequest`](src/mprpcprovider.cc#L249)

### 框架层失败与业务层失败分离

`MprpcController` 只承载**框架层**的失败(连接超时、序列化失败、网络错误),
业务层的成败由 Protobuf 响应里的错误码字段表达。调用方先查
`controller.Failed()`,再看业务返回——两类错误不混淆。

> 深度在:接口契约设计——让调用方能区分"RPC 没打通"和"业务拒绝了你"。
> 锚点 — [`MprpcController`](src/mprpccontroller.cc) ·
> [调用方用法](example/caller/calluserservice.cc#L41)

### 异步日志子系统(工程重点)

- **业务线程零阻塞**:日志走生产者/消费者模型,业务线程只做一次入队,
  写盘/flush 交给独立消费线程。
  锚点 — [`RpcLogger::Log`](src/logger.cc#L247) →
  [队列 `Push`](src/include/lockqueue.h#L21)
- **有界队列 + 丢最旧并计数**:洪峰下内存有界;被丢弃的条数会被统计,并以
  `[RPC][WARN] dropped N ...` 写进同一个日志 sink——既不无声丢失,也绝不
  反压业务线程。锚点 — [`RpcLockQueue`](src/include/lockqueue.h#L14) ·
  [丢弃可见化](src/logger.cc#L178)
- **优雅停机**:`停止标志 → 排空队列 → flush → fclose → join`,进程退出前
  把尾部日志全部落盘;生命周期由调用方显式 `Shutdown()` 控制,不依赖静态
  析构顺序。锚点 — [`Shutdown`](src/logger.cc#L207) ·
  [故意泄漏单例的原因](src/logger.cc#L199)
- **配置热路径优化**:消费线程用"原子版本号(epoch) + 互斥锁"读已锁定配置,
  每行仅一次无锁原子读,只有配置真正变更那一刻才加锁重读。
  锚点 — [`refreshConfig`](src/logger.cc#L111)

## 请求处理路径 · Request Flow

一次 `stub.Login(req, resp)` 从调用方到提供方再回来的完整主线:

```
调用方                                                          提供方
stub.Login(req, resp)
  │ Protobuf Stub 转发
  ▼
MprpcChannel::CallMethod                       [mprpcchannel.cc#L255]
  │ ① request 序列化 → argsString
  │ ② 组帧 [headerSize][RpcHeader(service/method/args_size)][args]
  │ ③ 查 ZooKeeper 发现 provider 地址(多实例则 round-robin 选一个)
  │ ④ 非阻塞 connect(带超时) + SendAll
  └──────────── TCP ────────────►  RpcProvider::onMessage   [mprpcprovider.cc#L207]
                                     │ ⑤ 拆帧,解析出 service / method / args
                                     │ ⑥ 查本地路由表 m_serviceInfoMap
                                     │ ⑦ argsString 反序列化为具体 request
                                     ▼
                                   service->CallMethod(...)
                                     │ Protobuf 分发到业务实现
                                     ▼
                                   UserService::Login   ← 你的业务代码
                                     │ 填 response,done->Run()
                                     ▼
                                   SendRpcResponse        [mprpcprovider.cc#L294]
  recv 直到对端关闭   ◄──────────── TCP ──────────────┘ ⑧ response 序列化回包,关闭连接
  │
  ▼
response->ParseFromString → 业务先查 controller.Failed(),再看业务错误码
```

> 关键角色:**Stub/Channel**(调用方组帧+发现+收发)、**RpcProvider**(提供方
> 拆帧+分发+回包)、**ZooKeeper**(只做服务发现)、**业务 Service**(真正逻辑)。
> 逐步骤的详细时序见 [docs/rpc-call-flow-notes.md](docs/rpc-call-flow-notes.md)。

## 效果演示 · Demo

### 端到端 RPC 调用(真实终端输出)

启动 `userservice` 后,提供方把每个方法注册为 ZooKeeper 顺序临时节点:

```text
[RPC][INFO] register service: UserServiceRpc
[RPC][INFO] register method: UserServiceRpc::Login
[RPC][INFO] register method: UserServiceRpc::Register
[RPC][INFO] zkclient start success! endpoints=127.0.0.1:2181
[RPC][INFO] register rpc provider node service=UserServiceRpc method=Login \
            path=/mprpc/UserServiceRpc/Login/providers/provider-0000000007 target=127.0.0.1:9000
[RPC][INFO] rpc provider start bind=127.0.0.1:9000 advertise=127.0.0.1:9000 threads=4
```

调用方发起 RPC,服务发现 → 连接 → 拿到业务结果:

```text
[RPC][INFO] rpc connect success! service=UserServiceRpc method=Login target=127.0.0.1:9000
rpc login response success：1
[RPC][INFO] rpc connect success! service=UserServiceRpc method=Register target=127.0.0.1:9000
rpc register response success:1
```

> 以上为真实运行输出,已省略 ZooKeeper C SDK 的启动环境噪音。

### 异步日志:有界丢弃可观测(真实测试输出)

向日志队列灌入 20 万条消息制造洪峰,队列有界、丢最旧,丢弃数被实时统计并
写进同一 sink;洪峰一过又恢复正常——丢失对调用方完全可见、可量化:

```text
[RPC][INFO] bounded-drop seq=0
[RPC][WARN] dropped 274 log message(s) due to full queue
[RPC][INFO] bounded-drop seq=275
[RPC][WARN] dropped 70 log message(s) due to full queue
[RPC][INFO] bounded-drop seq=346
...
[RPC][INFO] bounded-drop seq=199999          ← 洪峰退去,尾部连续无丢弃
```

同一套机制在停机场景下保证**尾部不丢**:推入 N 条后调用 `Shutdown()`,
消费线程排空队列、flush、fclose,落盘行数与推入数一致(尾条完整)。

## 协议示例 · Protocol

请求帧:`[4 字节 headerSize (网络字节序)][RpcHeader][args]`。`RpcHeader` 由
Protobuf 定义([src/rpcherder.proto](src/rpcherder.proto)):

```proto
message RpcHeader {
  bytes  service_name = 1;  // 服务名,如 "UserServiceRpc"
  bytes  method_name  = 2;  // 方法名,如 "Login"
  uint32 args_size    = 3;  // 业务参数序列化后的字节数
}
```

业务服务用标准 `.proto` 定义即可,框架不侵入业务消息
([example/user.proto](example/user.proto)):

```proto
service UserServiceRpc {
  rpc Login(LoginRequest)       returns (LoginResponse);
  rpc Register(RegisterRequest) returns (RegisterResponse);
}
```

响应帧目前是裸 Protobuf 字节流,调用方靠对端关闭连接(`recv == 0`)判定边界
——这是短连接模型的有意取舍,见下方设计决策表与 Roadmap。

## 架构与关键设计决策 · Architecture & Decisions

```
   调用方进程                      ZooKeeper                    提供方进程
┌───────────────┐            ┌──────────────┐            ┌────────────────┐
│ Stub          │  发现地址   │  注册中心     │   注册节点  │ RpcProvider    │
│  └ MprpcChannel│◄──────────►│ (仅服务发现)  │◄───────────│  └ Muduo Server│
│   序列化+组帧  │            └──────────────┘            │   拆帧+分发     │
└───────┬───────┘                                        └───────┬────────┘
        │                                                        ▼
        │         TCP [headerSize][RpcHeader][args]        业务 Service 实现
        └────────────────────────────────────────────────────────┘
                  ◄──────────── TCP [response bytes] ──────────────
```

| 设计决策 | 取舍与理由 |
| --- | --- |
| 复用 Protobuf `Service/Stub` | 不自造 IDL,业务方零学习成本;天然获得跨语言消息定义与高效序列化 |
| 自定义 4 字节定长头 + Protobuf 帧头 | 用最小协议解决 TCP 粘包;帧头本身也是 Protobuf,扩展字段无需改解析逻辑 |
| ZooKeeper 只做服务发现 | 注册中心不进数据面,业务流量直连提供方,避免注册中心成为转发瓶颈 |
| 提供方用顺序临时节点注册 | 临时节点随会话消失自动摘除故障实例;顺序节点天然支持同一方法多实例 |
| 调用方 round-robin 选址 | 多实例间均摊请求,负载均衡逻辑在调用方、可控可测 |
| 日志"丢最旧 + 计数"而非阻塞 | 日志绝不能拖慢 RPC 热路径;洪峰下宁可有界丢弃,但丢失必须可观测 |
| 当前为短连接(一次连接一次调用) | 实现简单、无需 response 定帧;长连接/连接池作为有意的演进项,见 Roadmap |

## 快速开始 · Quick Start

```bash
# 0. 依赖(Ubuntu / WSL):编译器、cmake、protobuf、zookeeper C 客户端
sudo apt install -y build-essential cmake pkg-config \
  libprotobuf-dev protobuf-compiler libzookeeper-mt-dev
# Muduo 需自行安装到 /usr/local(头文件 + libmuduo_net / libmuduo_base)

# 1. 启动注册中心(仅依赖服务跑在容器里,框架代码本地编译调试)
docker compose up -d zookeeper          # 或 ./scripts/deps-up.sh

# 2. 构建(框架 + 示例)
cmake -S . -B build
cmake --build build -j"$(nproc)"
# 产物:可执行文件在 build/bin/,静态库在 build/lib/libmprpc.a

# 3. 跑通端到端(两个终端)
./build/bin/userservice    -i config/local/userservice.conf   # 终端 A:提供方
./build/bin/calluserservice -i config/local/client.conf       # 终端 B:调用方
# 预期:rpc login response success：1 / rpc register response success:1
```

把框架交付给其他项目复用(导出头文件 + 静态库到 `dist/mprpc/`):

```bash
./scripts/pack.sh    # = 构建 + cmake --install build --prefix dist/mprpc
# 之后:头文件在 dist/mprpc/include/,静态库在 dist/mprpc/lib/libmprpc.a
```

### 配置

运行时用 `-i <conf>` 指定配置;所有键均支持环境变量覆盖,优先级
`env > 配置文件 > 内置默认`:

| 键 | 说明 |
| --- | --- |
| `RPC_BIND_IP` / `RPC_PORT` | 提供方监听地址 |
| `RPC_ADVERTISE_HOST` | 注册到 ZooKeeper 的对外地址(默认同 bind_ip) |
| `RPC_IO_THREADS` | Muduo I/O 线程数(默认 4) |
| `ZK_ENDPOINTS` / `MPRPC_ZK_NAMESPACE` | ZooKeeper 地址 / 服务注册根命名空间 |
| `RPC_CONNECT/SEND/RECV_TIMEOUT_MS` | 调用方连接 / 发送 / 接收超时 |
| `MPRPC_LOG_MODE` | 日志输出:`stdout` / `file`(按天滚动) |
| `MPRPC_LOG_DIR` / `MPRPC_LOG_NAME` | file 模式日志目录 / 服务标识 |
| `MPRPC_LOG_QUEUE_CAP` | 日志队列容量上限(满则丢最旧并计数,0 为不限) |

> file 模式日志路径:`<MPRPC_LOG_DIR>/<YYYY-MM-DD>-<MPRPC_LOG_NAME>.log`。

## 目录结构 · Layout

```
src/            框架核心(provider / channel / controller / config / zookeeper / logger)
src/include/    框架公开头文件(随静态库一起导出)
example/        示例:userservice / friendservice(提供方)与 call*(调用方)
config/local/   本地运行配置(-i <conf>)
scripts/        构建、打包、依赖编排脚本
docs/           调用链路时序笔记
compose.yaml    依赖服务(ZooKeeper)编排
```

## 演进路线 · Roadmap

当前版本已打通"定义服务 → 注册发现 → 跨进程调用 → 框架/业务错误分离"的完整
闭环,并把网络、协议、异步日志三块做扎实。下一步按价值分阶段推进:

- **连接复用与长连接**:当前为短连接(一次连接一次调用),计划引入连接池与
  长连接;长连接需为响应补 `response_size` 定帧、为并发请求补 `request_id`
  ——设计要点已在 [docs/rpc-call-flow-notes.md](docs/rpc-call-flow-notes.md) 中梳理。
- **自动化测试**:把现有手动验证的日志"不丢尾 / 有界丢弃"场景固化为可重复
  运行的单元/集成测试,纳入 CMake 与 CI。
- **服务发现增强**:调用方对 provider 列表做本地缓存 + Watch 增量更新,
  减少每次调用都查询 ZooKeeper 的开销。
- **可观测性**:补充调用耗时、成功率等指标,便于接入监控。

