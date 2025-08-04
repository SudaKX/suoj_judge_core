# Linux OJ 评测核心 - cgroup v2 版本使用说明

## cgroup v2 版本特性

cgroup v2 版本提供了更精确的内存监控，使用`memory.peak`获取进程运行期间的峰值内存使用量，比传统的 rusage 更加准确。

## 技术优势

### 1. 更精确的内存监控

- **memory.peak**: 记录 cgroup 中所有进程的峰值内存使用
- **memory.current**: 实时内存使用量
- **更准确的 MLE 检测**: 直接从内核获取准确的内存统计

### 2. 更强的隔离性

- **进程组隔离**: 使用 cgroup 将程序及其子进程完全隔离
- **内存限制**: 内核级别的内存限制，无法绕过
- **自动清理**: 程序结束后 cgroup 自动清理所有资源

### 3. 更好的安全性

- **防 fork 炸弹**: cgroup 可以限制进程总数
- **防内存泄漏**: 精确控制内存使用，超限自动杀死
- **资源可见性**: 完整的资源使用统计

## 使用要求

### 系统要求

- Linux 内核 5.0+ (支持 cgroup v2)
- systemd (大多数现代 Linux 发行版)
- root 权限 (创建和管理 cgroup 需要)

### 检查 cgroup v2 支持

```bash
# 检查cgroup v2是否挂载
ls /sys/fs/cgroup/

# 检查可用控制器
cat /sys/fs/cgroup/cgroup.controllers
```

### 权限要求

```bash

chmod +x run.sh
chmod +x judge_cgroup.sh
chmod +x judge_core_cgroup.cpp

# 需要使用sudo运行
sudo ./judge_cgroup.sh limits.json test.cpp test.in
```

## 文件说明

- **judge_core_cgroup.cpp**: cgroup v2 版本的评测核心
- **judge_cgroup.sh**: cgroup 版本的启动脚本
- **limits.json**: 配置文件（与普通版本兼容）

## 使用方法

### 基本用法

```bash
# 编译并运行（需要root权限）
sudo ./judge_cgroup.sh limits.json test.cpp test.in
```

### 手动编译

```bash
# 编译cgroup版本
g++ -g -std=c++20 -O2 -Wall -Wextra judge_core_cgroup.cpp -o judge_core_cgroup

# 运行（需要root权限）
sudo ./judge_core_cgroup limits.json test.cpp test.in
```

## 输出格式

```json
{
  "status": "OK",
  "time_used": 15, // ms
  "mem_used": 65536, // KB
  "exit_code": 0,
  "error_message": "",
  "stdout": "1 2 3\\n",
  "output_len": 6
}


{
  "status": "OK",
  "time_used": 15,
  "mem_used": 5040,
  "exit_code": 0,
  "error_message": "",
  "stdout": "",
  "output_len": 0
}

```

## cgroup v2 内存监控原理

### memory.peak

- 记录 cgroup 中所有进程的峰值物理内存使用量
- 包括程序本身、动态分配的内存、共享库等
- 比 rusage 的 ru_maxrss 更准确、更完整

### memory.current

- 当前实时内存使用量
- 用于调试和实时监控

### memory.max

- 设置内存使用上限
- 超限时内核会杀死进程（SIGKILL）

## 与普通版本的对比

| 特性       | 普通版本          | cgroup v2 版本   |
| ---------- | ----------------- | ---------------- |
| 内存监控   | rusage.ru_maxrss  | memory.peak      |
| 监控精度   | KB 级，可能不准确 | 字节级，非常准确 |
| 隔离程度   | setrlimit         | 内核级 cgroup    |
| 权限要求   | 普通用户          | 需要 root        |
| 子进程监控 | 有限              | 完全覆盖         |
| 资源清理   | 手动              | 自动             |

## 故障排除

### 权限问题

```bash
# 错误：Permission denied
# 解决：使用sudo
sudo ./judge_cgroup.sh limits.json test.cpp test.in
```

### cgroup 不支持

```bash
# 错误：cgroup v2不可用
# 检查：
ls /sys/fs/cgroup/
cat /proc/mounts | grep cgroup

# 确保使用较新的Linux内核和systemd
```

### 内存限制过小

```bash
# 错误：无法创建cgroup或程序立即被杀死
# 解决：增加limits.json中的memory_limit
{
  "memory_limit": 65536  # 应该在64MB或更多
}
```

## 性能说明

cgroup v2 版本的性能特点：

- **启动开销**: 略高（需要创建/删除 cgroup）
- **监控精度**: 大幅提升
- **安全性**: 显著增强
- **资源使用**: 轻微增加（cgroup 开销）
