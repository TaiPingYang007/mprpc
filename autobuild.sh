#!/bin/bash

# 只要有一步报错，立刻停止往下执行
set -e

# 如果没有 build 和 lib 目录，自动帮你建好（防止报错）
mkdir -p $(pwd)/build
mkdir -p $(pwd)/lib

# 1. 无情清理旧的编译垃圾
echo "=> 清理 build 目录..."
rm -rf $(pwd)/build/*

# 2. 进入 build 目录，执行 CMake 生成图纸，并 Make 开始编译
echo "=> 开始编译..."
cd $(pwd)/build &&
    cmake .. &&
    make

# 3. 编译完成后，退回根目录
cd ..

# 4. 把 src 下的 include 文件夹，整个拷贝到 lib 目录下
echo "=> 正在打包 SDK 到 lib 目录..."
cp -r $(pwd)/src/include $(pwd)/lib

echo "=> 恭喜！一键编译并打包完成！"