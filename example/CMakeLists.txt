# 让示例代码可以包含 example/ 目录下 proto 生成的头文件
include_directories(${PROJECT_SOURCE_DIR}/example)

add_subdirectory(callee)
add_subdirectory(caller)
