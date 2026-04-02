#include "./include/mprpccontroller.h"

// 重写controller内函数
MprpcController::MprpcController() : m_failed(false), m_errText("") {}

// 1. 重置 Controller 状态
void MprpcController::Reset() {
  m_failed = false;
  m_errText = "";
}

// 2. 判断 RPC 调用是否失败 true 发生问题，默认为false
bool MprpcController::Failed() const { return m_failed; }

// 3. 获取具体的错误信息
std::string MprpcController::ErrorText() const { return m_errText; }

// 4. 设置 RPC 调用失败及错误信息
void MprpcController::SetFailed(const std::string &reason) {
  m_failed = true;
  m_errText = reason;
}

// 5. 客户端主动发起取消 RPC 调用的请求（目前未实现）
void MprpcController::StartCancel() {}

// 6. 判断 RPC 调用是否被取消（目前未实现）
bool MprpcController::IsCanceled() const { return false; }

// 7. 注册取消回调（目前未实现）
void MprpcController::NotifyOnCancel(google::protobuf::Closure *callback) {}