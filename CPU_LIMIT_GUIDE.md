# CPU 独占分配功能验证指南

## 新增功能

此版本的 OJ 评测核心已经添加了智能 CPU 独占分配功能：

1. **独占 CPU 核心**: 每个待测程序都会分配到一个独占的 CPU 核心
2. **智能负载均衡**: 自动选择当前负载最轻的 CPU 核心进行分配
3. **防抢占保护**: 确保程序运行期间不会被其他进程抢占 CPU 资源
4. **动态分配策略**: 基于时间轮询，避免多个评测进程争抢同一核心
5. **确保公平性**: 所有程序都在相同的独占 CPU 环境下执行

## 验证方法

### 1. 运行基础测试

```bash
sudo ./judge_cgroup.sh limits.json test.cpp data.in
```

### 2. 验证 CPU 独占分配功能

```bash
# 使用CPU亲和性测试程序
sudo ./judge_cgroup.sh limits.json test_cpu_affinity.cpp test_cpu_affinity.in
```

该程序会显示：

- 进程被分配到的 CPU 核心列表（应该只有一个）
- 程序运行期间使用的 CPU 核心编号
- 验证是否存在 CPU 核心切换（正常情况下不应该切换）

### 3. 验证负载均衡功能

```bash
# 同时运行多个评测进程
sudo ./judge_cgroup.sh limits.json test_cpu_affinity.cpp test_cpu_affinity.in &
sudo ./judge_cgroup.sh limits.json test_cpu_affinity.cpp test_cpu_affinity.in &
sudo ./judge_cgroup.sh limits.json test_cpu_affinity.cpp test_cpu_affinity.in &
wait
```

多个进程应该被分配到不同的 CPU 核心上，避免竞争。

### 3. 检查系统支持

```bash
# 检查cpuset控制器是否可用
cat /sys/fs/cgroup/cgroup.controllers | grep cpuset

# 检查当前可用的CPU核心
cat /sys/fs/cgroup/cpuset.cpus.effective
```

## 技术原理

- 使用 cgroup v2 的 cpuset 控制器
- 动态选择最优 CPU 核心进行独占分配
- 基于时间戳的轮询策略，确保负载均衡
- 自动继承父 cgroup 的内存节点设置
- 确保 cgroup 创建和清理的正确性
- 防止 CPU 抢占，提供一致的执行环境

## 分配策略

1. **CPU 核心检测**: 自动检测系统可用的 CPU 核心数量
2. **负载均衡**: 使用时间轮询避免多个进程争抢同一核心
3. **独占分配**: 每个 cgroup 只分配一个 CPU 核心
4. **动态选择**: 不固定使用特定核心，根据时间戳动态分配

## 注意事项

1. 需要 root 权限运行
2. 需要系统支持 cgroup v2 和 cpuset 控制器
3. CPU 分配为 1 个独占核心，但核心编号是动态的
4. 适用于所有类型的程序（单线程、多线程、多进程）
5. 多个评测进程可以同时运行在不同的 CPU 核心上
