cmake_minimum_required(VERSION 3.10)
project(mprpc)

# 使用 C++11 标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Muduo 根目录，可通过 -DMUDUO_ROOT=<path> 覆盖
set(MUDUO_ROOT "/home/taipingyang/thirdparty/muduo"
    CACHE PATH "Muduo library root directory")

# 全局可执行文件输出目录
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
# 全局库文件输出目录
set(LIBRARY_OUTPUT_PATH    ${PROJECT_SOURCE_DIR}/lib)

# 全局头文件搜索路径
include_directories(
    ${PROJECT_SOURCE_DIR}/src/include   # mprpc 框架头文件
    ${MUDUO_ROOT}/include               # Muduo 头文件
)

# 全局链接路径（Muduo 预编译库所在目录）
link_directories(
    ${MUDUO_ROOT}/lib
)

# 递归构建子目录
add_subdirectory(src)
add_subdirectory(example)
