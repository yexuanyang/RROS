#!/bin/bash
export KBUILD_EXPORT_COMPILE_COMMANDS=1
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
export LLVM=1
export CC=clang
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- LLVM=1 CC=clang -j`nproc`
make compile_commands.json
