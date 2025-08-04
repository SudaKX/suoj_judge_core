#!/bin/bash

# OJ评测核心一键启动脚本 (使用cgroup v2)

# 检查参数
if [ $# -ne 3 ]; then
    echo "用法: $0 <limits_file> <source_file> <input_file>"
    echo "示例: $0 limits.json test.cpp test.in"
    exit 1
fi

LIMITS_FILE="$1"
SOURCE_FILE="$2"
INPUT_FILE="$3"

# 检查是否有root权限
if [ "$EUID" -ne 0 ]; then
    echo "错误: 使用cgroup需要root权限"
    echo "请使用 sudo 运行此脚本"
    exit 1
fi

# 检查cgroup v2是否可用
if [ ! -d "/sys/fs/cgroup" ]; then
    echo "错误: cgroup v2不可用，请确保系统支持cgroup v2"
    exit 1
fi

# 检查文件是否存在
if [ ! -f "$LIMITS_FILE" ]; then
    echo "错误: 限制文件 '$LIMITS_FILE' 不存在"
    exit 1
fi

if [ ! -f "$SOURCE_FILE" ]; then
    echo "错误: 源代码文件 '$SOURCE_FILE' 不存在"
    exit 1
fi

if [ ! -f "$INPUT_FILE" ]; then
    echo "错误: 输入文件 '$INPUT_FILE' 不存在"
    exit 1
fi


# 编译judge_core_cgroup
echo "正在编译评测核心 (cgroup v2版本)..."
g++ -g -std=c++20 -O2 -Wall -Wextra judge_core_cgroup.cpp -o judge_core_cgroup

if [ $? -ne 0 ]; then
    echo "错误: 编译judge_core_cgroup失败"
    exit 1
fi

echo "开始评测..."

# 运行评测
./judge_core_cgroup "$LIMITS_FILE" "$SOURCE_FILE" "$INPUT_FILE"

# 保存评测结果到文件
RESULT_FILE="result_cgroup_$(date +%Y%m%d_%H%M%S).json"
./judge_core_cgroup "$LIMITS_FILE" "$SOURCE_FILE" "$INPUT_FILE" > "$RESULT_FILE"

echo ""
echo "评测完成，结果已保存到: $RESULT_FILE"

清理编译产生的文件
rm -f judge_core_cgroup

echo "评测核心已清理完毕"


