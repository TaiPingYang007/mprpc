# RPC 调用链路复习笔记

这份笔记用于快速复习 `03_rpc_framework` 的 RPC 调用流程，重点服务于后续理解和设计 BridgeIM 内部 RPC 通信。

## 核心结论

```text
1. Stub + MprpcChannel 在调用方：把本地调用打包成网络请求。
2. RpcProvider + Service 在提供方：把网络请求拆包并分发成本地业务方法。
3. ZooKeeper 只负责服务发现，不转发业务请求。
```

## 协议边界提醒

```text
BridgeIM 外部客户端协议：JSON + '\n'
BridgeIM 未来内部服务通信：Protobuf + RPC
```

两者不要混在一起。外部客户端协议负责客户端到网关/服务器；内部 RPC 协议负责服务之间通信。

## 一次 `stub.Login(...)` 完整时序图

```text
客户端进程 Caller                              ZooKeeper                         服务端进程 Provider
──────────────────────────────────────────────────────────────────────────────────────────────

1. 程序启动
MprpcApplication::Init()
读取配置
  |
  |
  |                                                                       MprpcApplication::Init()
  |                                                                       读取配置
  |                                                                       |
  |                                                                       v
  |                                                                       RpcProvider provider
  |                                                                       provider.NotifyService(new UserService())
  |                                                                       |
  |                                                                       | 本地注册：
  |                                                                       | m_serviceInfoMap["UserServiceRpc"]
  |                                                                       |   -> UserService 对象
  |                                                                       |   -> Login/Register 方法表
  |                                                                       v
  |                                                                       provider.Run()
  |                                                                       |
  |                                                                       | 注册 ZooKeeper：
  |                                                                       | /mprpc/UserServiceRpc/Login/providers/provider-xxx
  |                                                                       |   = "127.0.0.1:8000"
  |                                                                       |
  |                                                                       | /mprpc/UserServiceRpc/Register/providers/provider-xxx
  |                                                                       |   = "127.0.0.1:8000"
  |                                                                       v
  |                                                                       启动 muduo TcpServer
  |                                                                       等待客户端连接


2. 客户端发起 Login RPC

fixbug::UserServiceRpc_Stub stub(new MprpcChannel())
  |
  v
构造 LoginRequest
  name = "zhang san"
  pwd  = "123456"
  |
  v
构造 LoginResponse
构造 MprpcController
  |
  v
stub.Login(&controller, &request, &response, nullptr)
  |
  | Protobuf Stub 内部转发
  v
MprpcChannel::CallMethod(method, controller, request, response, done)
  |
  | method 描述当前调用的是：
  | service_name = "UserServiceRpc"
  | method_name  = "Login"
  |
  | request 实际类型：
  | LoginRequest
  v
request.SerializeToString(argsString)
  |
  | LoginRequest 对象
  |   -> 二进制 argsString
  v
构造 RpcHeader
  service_name = "UserServiceRpc"
  method_name  = "Login"
  args_size    = argsString.size()
  |
  v
rpcHeader.SerializeToString(rpcHeaderString)
  |
  v
组装 sendRpcString
  [4字节 headerSize][rpcHeaderString][argsString]


3. 客户端服务发现

MprpcChannel 创建 ZkClient
  |
  v
连接 ZooKeeper
  |
  | 查询：
  | /mprpc/UserServiceRpc/Login/providers
  | ───────────────────────────────────────────────►
  |                                                |
  |                                                | 返回 provider 子节点列表
  | ◄───────────────────────────────────────────────
  |
  | 查询某个 provider 节点数据
  | ───────────────────────────────────────────────►
  |                                                |
  |                                                | 返回 "127.0.0.1:8000"
  | ◄───────────────────────────────────────────────
  |
  v
ParseHostData("127.0.0.1:8000")
  host = "127.0.0.1"
  port = 8000


4. 客户端连接 RpcProvider 并发送请求

创建 socket
  |
  v
connect 127.0.0.1:8000
  |──────────────────────────────────────────────────────────────────────────────►
  |                                                                       muduo TcpServer 接受连接
  |                                                                       生成 TcpConnection conn
  |
  v
SendAll(sendRpcString)
  |──────────────────────────────────────────────────────────────────────────────►
  |                                                                       onMessage(conn, buffer, time)


5. 服务端拆包

                                                                        RpcProvider::onMessage
                                                                        |
                                                                        | buffer 中收到：
                                                                        | [headerSize][RpcHeader][args]
                                                                        v
                                                                        读取 4 字节 headerSize
                                                                        |
                                                                        v
                                                                        根据 headerSize 取出 rpcHeaderString
                                                                        |
                                                                        v
                                                                        RpcHeader.ParseFromString(...)
                                                                        |
                                                                        v
                                                                        得到：
                                                                        serviceName = "UserServiceRpc"
                                                                        methodName  = "Login"
                                                                        argsSize    = ...
                                                                        |
                                                                        v
                                                                        根据 argsSize 取出 argsString
                                                                        |
                                                                        v
                                                                        ExecuteRpcRequest(conn,
                                                                                          serviceName,
                                                                                          methodName,
                                                                                          argsString)


6. 服务端查本地路由表并创建 request/response

                                                                        ExecuteRpcRequest
                                                                        |
                                                                        v
                                                                        m_serviceInfoMap.find("UserServiceRpc")
                                                                        |
                                                                        | 找到：
                                                                        | service = UserService 对象
                                                                        | methodMap = Login/Register 表
                                                                        v
                                                                        methodMap.find("Login")
                                                                        |
                                                                        | 找到 Login MethodDescriptor
                                                                        v
                                                                        service->GetRequestPrototype(method).New()
                                                                        |
                                                                        | 创建 LoginRequest 对象
                                                                        v
                                                                        request->ParseFromString(argsString)
                                                                        |
                                                                        | argsString -> LoginRequest
                                                                        | name = "zhang san"
                                                                        | pwd  = "123456"
                                                                        v
                                                                        service->GetResponsePrototype(method).New()
                                                                        |
                                                                        | 创建 LoginResponse 对象
                                                                        v
                                                                        创建 MprpcController
                                                                        创建 done 回调：
                                                                        done = SendRpcResponse(conn, callContext)


7. 服务端调用真正业务方法

                                                                        service->CallMethod(method,
                                                                                            controller,
                                                                                            request,
                                                                                            response,
                                                                                            done)
                                                                        |
                                                                        | Protobuf 根据 method 分发
                                                                        v
                                                                        UserService::Login(controller,
                                                                                           LoginRequest*,
                                                                                           LoginResponse*,
                                                                                           done)
                                                                        |
                                                                        v
                                                                        读取 request:
                                                                        name = "zhang san"
                                                                        pwd  = "123456"
                                                                        |
                                                                        v
                                                                        LoginLocal(name, pwd)
                                                                        |
                                                                        v
                                                                        填 response:
                                                                        result.errcode = 0
                                                                        result.errormasg = ""
                                                                        success = true
                                                                        |
                                                                        v
                                                                        done->Run()


8. 服务端发送响应

                                                                        done->Run()
                                                                        |
                                                                        v
                                                                        RpcProvider::SendRpcResponse(conn, callContext)
                                                                        |
                                                                        v
                                                                        response.SerializeToString(responseString)
                                                                        |
                                                                        | LoginResponse -> 二进制 responseString
                                                                        v
                                                                        conn->send(responseString)
  |◄──────────────────────────────────────────────────────────────────────────────
                                                                        |
                                                                        v
                                                                        conn->shutdown()


9. 客户端接收响应

MprpcChannel::CallMethod
  |
  | recv(...)
  | 一直读，直到服务端关闭连接，recv 返回 0
  v
得到 responseString
  |
  v
response->ParseFromString(responseString)
  |
  | 二进制 responseString -> LoginResponse 对象
  v
CallMethod 返回
  |
  v
客户端业务代码继续执行
  |
  v
if (controller.Failed())
  判断 RPC 框架层是否失败
  |
  v
if (response.result().errcode() != 0)
  判断业务层是否失败
  |
  v
使用 response.sucess()
```

## 精简版链路

```text
客户端：
stub.Login()
  ↓
MprpcChannel::CallMethod
  ↓
request 序列化
  ↓
RpcHeader(service/method/args_size)
  ↓
查 ZooKeeper 得到 provider ip:port
  ↓
TCP 发送 [headerSize][RpcHeader][args]

服务端：
RpcProvider::onMessage
  ↓
拆 [headerSize][RpcHeader][args]
  ↓
得到 serviceName / methodName / argsString
  ↓
查 m_serviceInfoMap 和 methodMap
  ↓
创建具体 request/response
  ↓
argsString 反序列化为 request
  ↓
service->CallMethod(...)
  ↓
UserService::Login
  ↓
填 response
  ↓
done->Run()
  ↓
SendRpcResponse

客户端：
recv responseString
  ↓
ParseFromString 到 response
  ↓
业务检查 controller 和 response
```

## 重要概念区分

| 概念 | 职责 |
| --- | --- |
| Stub | 客户端代理，让业务像调用本地函数一样调用远程方法 |
| MprpcChannel | 客户端通信通道，负责组包、查 ZooKeeper、TCP 发送、接收响应 |
| RpcProvider | 服务端 RPC 服务器，负责发布服务、收包、拆包、分发、回包 |
| Service 实现类 | 真正业务逻辑，例如 `UserService::Login` |
| ZooKeeper | 注册中心，负责服务发现，不转发业务请求 |
| MprpcController | 记录 RPC 框架层错误，不代表业务错误 |
| request/response | Protobuf 业务请求/响应消息 |
| done | response 填好后通知框架发送响应的回调 |

## RPC 失败与业务失败

```text
RPC 框架层失败：
- ZooKeeper 连接失败
- 服务发现失败
- TCP 连接失败
- send/recv 失败
- request/response 序列化或反序列化失败

查看：controller.Failed() / controller.ErrorText()

业务层失败：
- 用户名密码错误
- 用户不存在
- 数据库查询失败
- 权限不足

查看：response.result().errcode() / response.result().errormasg()
```

## 当前协议特点与局限

```text
请求协议：
[4字节 headerSize][RpcHeader][args]

RpcHeader：
service_name
method_name
args_size

响应协议：
[response protobuf bytes]
然后服务端关闭连接。
```

当前响应没有 `responseSize`，因为框架采用短连接：一次连接只承载一次 RPC 请求和一次 RPC 响应，客户端靠 `recv == 0` 判断响应结束。

如果未来要改成长连接，至少需要：

```text
1. response size / frame size：标识响应边界
2. request_id：在同一连接并发多个请求时匹配响应和请求
```
