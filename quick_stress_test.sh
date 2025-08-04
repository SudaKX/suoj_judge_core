#!/bin/bash

# 快速压力测试脚本（用于验证功能）
# 3个并发进程，2轮测试

set -e

CONCURRENT_PROCESSES=3
TEST_ROUNDS=2
LIMITS_FILE="limits.json"
SOURCE_FILE="test_cpu_affinity.cpp"
INPUT_FILE="test_cpu_affinity.in"

echo "=== 快速压力测试 ==="
echo "并发进程数: $CONCURRENT_PROCESSES"
echo "测试轮数: $TEST_ROUNDS"

# 检查root权限
if [ "$EUID" -ne 0 ]; then
    echo "错误: 需要root权限"
    echo "请使用: sudo $0"
    exit 1
fi

# 创建结果目录
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RESULT_DIR="quick_test_results_$TIMESTAMP"
mkdir -p "$RESULT_DIR"

echo "结果目录: $RESULT_DIR"
echo ""

# 数据数组
declare -a memory_data=()
declare -a time_data=()
declare -a cpu_data=()

for round in $(seq 1 $TEST_ROUNDS); do
    echo "=== 第 $round 轮测试 ==="
    
    round_dir="$RESULT_DIR/round_$round"
    mkdir -p "$round_dir"
    
    # 启动并发进程
    pids=()
    for i in $(seq 1 $CONCURRENT_PROCESSES); do
        echo "启动进程 $i..."
        ./judge_cgroup.sh "$LIMITS_FILE" "$SOURCE_FILE" "$INPUT_FILE" > "$round_dir/process_${i}_result.json" 2>&1 &
        pids+=($!)
        sleep 0.1
    done
    
    echo "等待所有进程完成..."
    for pid in "${pids[@]}"; do
        wait $pid
    done
    
    # 收集数据
    for i in $(seq 1 $CONCURRENT_PROCESSES); do
        result_file="$round_dir/process_${i}_result.json"
        if [ -f "$result_file" ]; then
            # 提取数据
            memory=$(grep -o '"mem_used":[^,]*' "$result_file" | cut -d':' -f2 | tr -d ' ')
            time_used=$(grep -o '"time_used":[^,]*' "$result_file" | cut -d':' -f2 | tr -d ' ')
            cpu_allocated=$(grep -o '"allocated_cpu":"[^"]*"' "$result_file" | cut -d'"' -f4)
            
            if [ -n "$memory" ]; then
                memory_data+=($memory)
            fi
            if [ -n "$time_used" ]; then
                time_data+=($time_used)
            fi
            if [ -n "$cpu_allocated" ]; then
                cpu_data+=($cpu_allocated)
            fi
            
            echo "进程 $i: 内存=${memory}B, 时间=${time_used}ms, CPU=${cpu_allocated}"
        fi
    done
    echo ""
done

# 输出数组数据
echo "=== 收集的数据数组 ==="
echo ""

echo "内存数据数组 (字节):"
printf "memory_data=("
for mem in "${memory_data[@]}"; do
    printf "%s " "$mem"
done
printf ")\n"

echo ""
echo "时间数据数组 (毫秒):"
printf "time_data=("
for time in "${time_data[@]}"; do
    printf "%s " "$time"
done
printf ")\n"

echo ""
echo "CPU分配数据数组:"
printf "cpu_data=("
for cpu in "${cpu_data[@]}"; do
    printf "%s " "$cpu"
done
printf ")\n"

echo ""
echo "=== 数据统计 ==="

# 内存统计
if [ ${#memory_data[@]} -gt 0 ]; then
    memory_sum=0
    memory_max=${memory_data[0]}
    memory_min=${memory_data[0]}
    
    for mem in "${memory_data[@]}"; do
        memory_sum=$((memory_sum + mem))
        if [ $mem -gt $memory_max ]; then
            memory_max=$mem
        fi
        if [ $mem -lt $memory_min ]; then
            memory_min=$mem
        fi
    done
    
    memory_avg=$((memory_sum / ${#memory_data[@]}))
    
    echo "内存统计:"
    echo "  样本数: ${#memory_data[@]}"
    echo "  平均值: $memory_avg 字节 ($(echo "scale=2; $memory_avg / 1024" | bc) KB)"
    echo "  最大值: $memory_max 字节 ($(echo "scale=2; $memory_max / 1024" | bc) KB)"
    echo "  最小值: $memory_min 字节 ($(echo "scale=2; $memory_min / 1024" | bc) KB)"
fi

echo ""

# 时间统计
if [ ${#time_data[@]} -gt 0 ]; then
    time_sum=0
    time_max=${time_data[0]}
    time_min=${time_data[0]}
    
    for time in "${time_data[@]}"; do
        time_sum=$((time_sum + time))
        if [ $time -gt $time_max ]; then
            time_max=$time
        fi
        if [ $time -lt $time_min ]; then
            time_min=$time
        fi
    done
    
    time_avg=$((time_sum / ${#time_data[@]}))
    
    echo "时间统计:"
    echo "  样本数: ${#time_data[@]}"
    echo "  平均值: $time_avg 毫秒"
    echo "  最大值: $time_max 毫秒"
    echo "  最小值: $time_min 毫秒"
fi

echo ""

# CPU分配统计
if [ ${#cpu_data[@]} -gt 0 ]; then
    echo "CPU分配统计:"
    echo "  样本数: ${#cpu_data[@]}"
    echo "  分配情况:"
    for cpu in $(printf '%s\n' "${cpu_data[@]}" | sort -n | uniq); do
        count=$(printf '%s\n' "${cpu_data[@]}" | grep -c "^$cpu$")
        echo "    CPU $cpu: $count 次"
    done
fi

echo ""
echo "快速测试完成！结果保存在: $RESULT_DIR"
