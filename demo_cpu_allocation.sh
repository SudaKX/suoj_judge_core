#!/bin/bash

# CPU独占分配功能演示脚本

echo "=== OJ评测核心 CPU独占分配功能演示 ==="
echo ""

# 检查系统CPU核心数
CPU_COUNT=$(nproc)
echo "系统CPU核心数: $CPU_COUNT"
echo ""

# 检查cgroup v2支持
if [ ! -d "/sys/fs/cgroup" ]; then
    echo "错误: 系统不支持cgroup v2"
    exit 1
fi

# 检查cpuset控制器
if ! cat /sys/fs/cgroup/cgroup.controllers | grep -q cpuset; then
    echo "错误: 系统不支持cpuset控制器"
    exit 1
fi

echo "✓ 系统支持cgroup v2和cpuset控制器"
echo ""

# 检查是否有root权限
if [ "$EUID" -ne 0 ]; then
    echo "注意: 需要root权限运行演示"
    echo "请使用: sudo $0"
    exit 1
fi

echo "=== 单个程序CPU分配测试 ==="
echo "运行CPU亲和性测试程序..."
./judge_cgroup.sh limits.json test_cpu_affinity.cpp test_cpu_affinity.in

echo ""
echo "=== 并行程序负载均衡测试 ==="
echo "同时运行3个程序，观察CPU分配..."

# 创建临时结果文件
TEMP_DIR="/tmp/oj_cpu_test_$$"
mkdir -p "$TEMP_DIR"

# 并行运行3个程序
for i in 1 2 3; do
    echo "启动测试进程 $i..."
    ./judge_cgroup.sh limits.json test_cpu_affinity.cpp test_cpu_affinity.in > "$TEMP_DIR/result_$i.json" 2>&1 &
    sleep 1
done

# 等待所有进程完成
wait

echo "所有进程完成，结果："
for i in 1 2 3; do
    echo "--- 进程 $i 结果 ---"
    if [ -f "$TEMP_DIR/result_$i.json" ]; then
        # 提取并显示分配的CPU核心
        ALLOCATED_CPU=$(grep -o '"allocated_cpu":"[^"]*"' "$TEMP_DIR/result_$i.json" | cut -d'"' -f4)
        echo "分配的CPU核心: $ALLOCATED_CPU"
        echo "完整结果:"
        cat "$TEMP_DIR/result_$i.json" | jq . 2>/dev/null || cat "$TEMP_DIR/result_$i.json"
    fi
    echo ""
done

# 清理临时文件
rm -rf "$TEMP_DIR"

echo "=== 演示完成 ==="
echo "CPU独占分配功能验证完毕！"
