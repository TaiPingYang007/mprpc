#pragma once
#include <google/protobuf/service.h>
#include <string>

// MprpcController: RPC 调用的状态控制器
// 它的核心作用是：在 RPC
// 调用的整个生命周期中，记录调用是否成功，以及记录框架底层的错误信息（如网络断开、序列化失败等）。
class MprpcController : public google::protobuf::RpcController {
public:
  // 重写controller内函数
  MprpcController(); // 建议加上构造函数，用来初始化 m_failed(false) 和
                     // m_errText("")

  // 1. 重置 Controller 状态
  // 作用：让这个 Controller 对象可以被复用。调用后，m_failed 恢复为
  // false，错误信息清空。
  void Reset();

  // 2. 判断 RPC 调用是否失败
  // 作用：客户端在 CallMethod 执行完毕后，第一件事就是调用这个函数。
  // 如果返回 true，说明整个 RPC
  // 调用在网络或框架层就夭折了，连业务逻辑都没走到。
  bool Failed() const;

  // 3. 获取具体的错误信息
  // 作用：如果 Failed() 返回
  // true，客户端调用这个函数就能知道具体是哪里挂了（比如打印出 "send
  // error!"）。
  std::string ErrorText() const;

  // 4. 设置 RPC 调用失败及错误信息
  // 作用：这个函数通常是由
  // MprpcChannel（框架层）在执行网络收发报错时调用的，用来给黑匣子写入报错原因。
  void SetFailed(const std::string &reason);

  // 目前未实现具体功能
  // 5. 客户端主动发起取消 RPC 调用的请求（目前未实现）
  void StartCancel();
  // 6. 判断 RPC 调用是否被取消（目前未实现）
  bool IsCanceled() const;
  // 7. 注册取消回调（目前未实现）
  void NotifyOnCancel(google::protobuf::Closure *callback);

private:
  bool m_failed;         // RPC方法执行过程中的状态
  std::string m_errText; // RPC方法执行过程中的错误信息
};