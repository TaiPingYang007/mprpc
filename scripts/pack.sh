#!/usr/bin/env bash
set -euo pipefail

# 构建 + 打包：编译框架，并把头文件和静态库导出到 dist/mprpc，
# 供其他项目（如 BridgeIM）通过 dist/mprpc/include 和 dist/mprpc/lib 接入。
# 用于「交付时」一步刷新 dist/；日常本地开发用 build-local.sh 即可。

cmake -S . -B build
cmake --build build -j"$(nproc)"
cmake --install build --prefix dist/mprpc
