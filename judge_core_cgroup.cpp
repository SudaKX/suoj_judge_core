/**
 * @file judge_core_cgroup.cpp
 * @brief Linux OJ评测核心 - cgroup v2版本
 * @author OJ Core Team
 * @version 2.0
 * @date 2025-08-04
 *
 * @details 基于Linux cgroup v2技术实现的高精度OJ评测核心
 *          提供字节级内存监控和内核级进程隔离
 *
 * 核心特性：
 * - 使用cgroup v2进行精确的资源控制和监控
 * - 通过memory.peak获取峰值内存使用量
 * - 支持多种评测状态：OK、TLE、MLE、RE、CE、OLE
 * - 提供JSON格式的详细评测结果
 *
 * 技术栈：
 * - cgroup v2: 内核级资源管理
 * - setrlimit: POSIX资源限制
 * - fork/exec: 进程隔离执行
 * - pipe/select: 进程间通信
 *
 * @warning 需要root权限运行
 * @note 要求Linux内核5.0+和systemd支持
 */

#include <iostream>       // 标准输入输出流
#include <fstream>        // 文件流操作
#include <string>         // 字符串处理
#include <chrono>         // 高精度时间测量
#include <vector>         // 动态数组容器
#include <functional>     // 哈希函数支持
#include <unistd.h>       // POSIX系统调用
#include <sys/wait.h>     // 进程等待相关
#include <sys/resource.h> // 资源限制
#include <sys/time.h>     // 时间相关系统调用
#include <signal.h>       // 信号处理
#include <fcntl.h>        // 文件控制
#include <sys/stat.h>     // 文件状态
#include <sched.h>        // CPU调度和亲和性
#include <sstream>        // 字符串流
#include <random>         // 随机数生成

using namespace std;
using namespace std::chrono;

/**
 * @struct JudgeResult
 * @brief 评测结果数据结构
 *
 * 包含程序执行的完整评测信息，用于向外部系统返回评测结果
 */
struct JudgeResult
{
    string status;         ///< 评测状态：OK/TLE/MLE/RE/CE/OLE/SE
    long long time_used;   ///< 实际执行时间(毫秒)
    long long mem_used;    ///< 峰值内存使用量(字节，来自memory.peak)
    int exit_code;         ///< 程序退出代码
    string error_message;  ///< 详细错误信息
    string stdout_content; ///< 程序标准输出内容
    int output_len;        ///< 输出内容长度(字节)
    string allocated_cpu;  ///< 分配的CPU核心编号
};

/**
 * @struct Limits
 * @brief 资源限制配置结构体
 *
 * 定义程序执行时的各种资源限制，确保系统安全和公平评测
 */
struct Limits
{
    int time_limit;         ///< CPU时间限制(毫秒)
    long long memory_limit; ///< 内存使用限制(字节)
    int output_limit;       ///< 输出大小限制(字节)
    int compile_timeout;    ///< 编译超时时间(毫秒)
    long long stack_limit;  ///< 栈大小限制(字节)
};

/**
 * @class CgroupManager
 * @brief cgroup v2管理器类
 *
 * 封装cgroup v2的创建、配置、监控和清理操作
 * 提供面向对象的cgroup管理接口，确保资源正确释放
 *
 * 主要功能：
 * - 创建和删除cgroup
 * - 设置内存限制
 * - 添加进程到cgroup
 * - 监控内存使用情况
 * - 自动资源清理
 */
class CgroupManager
{
private:
    string cgroup_path; ///< cgroup在文件系统中的完整路径
    string cgroup_name; ///< cgroup名称（唯一标识符）
    bool created;       ///< cgroup是否已成功创建

public:
    /**
     * @brief 构造函数
     *
     * 生成随机的cgroup名称，避免多个评测进程之间的冲突
     * cgroup路径格式：/sys/fs/cgroup/judge_XXXXXX
     */
    CgroupManager() : created(false)
    {
        // 生成随机的cgroup名称，确保唯一性
        random_device rd;
        mt19937 gen(rd());
        uniform_int_distribution<> dis(100000, 999999);
        cgroup_name = "judge_" + to_string(dis(gen));
        cgroup_path = "/sys/fs/cgroup/" + cgroup_name;
    }

    /**
     * @brief 析构函数
     *
     * 自动清理cgroup资源，防止资源泄漏
     * 使用RAII模式确保异常安全
     */
    ~CgroupManager()
    {
        cleanup();
    }

    /**
     * @brief 创建cgroup
     * @return bool 创建成功返回true，失败返回false
     *
     * 在/sys/fs/cgroup下创建新的cgroup目录
     * 需要root权限才能成功执行
     *
     * @note 创建失败通常是由于权限不足或cgroup v2未启用
     */
    bool create()
    {
        // 创建cgroup目录，权限设置为755
        if (mkdir(cgroup_path.c_str(), 0755) != 0)
        {
            return false;
        }
        created = true;
        return true;
    }

    /**
     * @brief 设置内存限制
     * @param limit_bytes 内存限制值(字节)
     * @return bool 设置成功返回true，失败返回false
     *
     * 向memory.max文件写入内存限制值
     * 超过此限制的进程将被内核杀死(SIGKILL)
     *
     * @warning limit_bytes必须是正数，过小的值可能导致进程无法启动
     */
    bool setMemoryLimit(long long limit_bytes)
    {
        if (!created)
            return false;

        ofstream memory_max(cgroup_path + "/memory.max");
        if (!memory_max)
            return false;

        memory_max << limit_bytes << endl;
        return memory_max.good();
    }

    /**
     * @brief 设置CPU限制 - 严格固定在单个CPU核心
     * @return bool 设置成功返回true，失败返回false
     *
     * 为待测程序分配一个固定的CPU核心，确保整个运行期间严格在该核心上执行
     * 禁止进程在不同CPU核心之间迁移，提供完全一致的执行环境
     *
     * @details cpuset.cpus控制进程可以使用的CPU核心
     *          - 选择一个可用的CPU核心并严格固定
     *          - 设置CPU亲和性确保不会迁移
     *          - 保证所有程序获得相同的单核心执行条件
     *
     * @note 严格单核心执行确保评测的绝对公平性
     */
    bool setCpuLimit()
    {
        if (!created)
            return false;

        // 首先确保在根cgroup中启用cpuset控制器
        ofstream root_subtree("/sys/fs/cgroup/cgroup.subtree_control");
        if (root_subtree)
        {
            root_subtree << "+cpuset" << endl;
            root_subtree.close();
        }

        // 选择一个CPU核心进行严格绑定
        string selected_cpu = selectCpuForBinding();
        if (selected_cpu.empty())
        {
            return false;
        }

        // 设置cpuset.cpus - 严格限制在选定的单个CPU核心
        ofstream cpuset_cpus(cgroup_path + "/cpuset.cpus");
        if (!cpuset_cpus)
            return false;

        cpuset_cpus << selected_cpu << endl;

        if (!cpuset_cpus.good())
        {
            return false;
        }

        // 设置cpuset.mems - 继承内存节点设置
        ifstream parent_mems("/sys/fs/cgroup/cpuset.mems.effective");
        string available_mems;
        if (parent_mems)
        {
            getline(parent_mems, available_mems);
            parent_mems.close();
        }

        if (available_mems.empty())
        {
            available_mems = "0";
        }

        ofstream cpuset_mems(cgroup_path + "/cpuset.mems");
        if (!cpuset_mems)
            return false;

        cpuset_mems << available_mems << endl;
        return cpuset_mems.good();
    }

    /**
     * @brief 强制CPU绑定到指定核心
     * @param pid 进程ID
     * @param cpu_id CPU核心ID
     * @return bool 绑定成功返回true，失败返回false
     *
     * 使用sched_setaffinity直接设置进程的CPU亲和性
     * 这是对cgroup cpuset的补充，确保更严格的CPU绑定
     */
    bool forceCpuBinding(pid_t pid, int cpu_id)
    {
        cpu_set_t cpu_set;
        CPU_ZERO(&cpu_set);
        CPU_SET(cpu_id, &cpu_set);

        if (sched_setaffinity(pid, sizeof(cpu_set), &cpu_set) == 0)
        {
            return true;
        }
        return false;
    }

private:
    /**
     * @brief 选择CPU核心进行严格绑定
     * @return string 选定的CPU核心编号，失败返回空字符串
     *
     * 基于轮询策略选择一个CPU核心，确保多个评测进程分散到不同核心
     * 但每个进程都严格固定在其分配的核心上
     *
     * @details 选择策略：
     *          1. 获取系统可用CPU核心数量
     *          2. 使用时间戳进行轮询分配
     *          3. 确保选择的核心有效且可用
     *          4. 返回单个核心编号用于严格绑定
     */
    string selectCpuForBinding()
    {
        // 获取系统CPU核心数量
        int cpu_count = getCpuCount();
        if (cpu_count <= 0)
        {
            return "0"; // 默认使用CPU 0
        }

        // 使用时间戳进行轮询，确保不同时间启动的进程分散到不同核心
        auto now = chrono::high_resolution_clock::now();
        auto timestamp = now.time_since_epoch().count();

        // 基于cgroup名称和时间戳计算，增加随机性
        size_t hash_value = std::hash<string>{}(cgroup_name) ^ timestamp;
        int selected_cpu = hash_value % cpu_count;

        return to_string(selected_cpu);
    }

    /**
     * @brief 获取系统CPU核心数量
     * @return int CPU核心数量，失败返回-1
     *
     * 通过读取/proc/cpuinfo获取系统的CPU核心数量
     * 用于CPU分配时的轮询计算
     */
    int getCpuCount()
    {
        ifstream cpuinfo("/proc/cpuinfo");
        if (!cpuinfo)
        {
            return -1;
        }

        int cpu_count = 0;
        string line;
        while (getline(cpuinfo, line))
        {
            if (line.find("processor") == 0)
            {
                cpu_count++;
            }
        }
        cpuinfo.close();

        return cpu_count > 0 ? cpu_count : 1;
    }

public:
    /**
     * @brief 将进程添加到cgroup
     * @param pid 要添加的进程ID
     * @return bool 添加成功返回true，失败返回false
     *
     * 向cgroup.procs文件写入进程ID，使该进程受cgroup限制
     * 进程的所有子进程也会自动受到相同限制
     *
     * @note 进程必须存在且调用者有权限操作该进程
     */
    bool addProcess(pid_t pid)
    {
        if (!created)
            return false;

        ofstream cgroup_procs(cgroup_path + "/cgroup.procs");
        if (!cgroup_procs)
            return false;

        cgroup_procs << pid << endl;
        return cgroup_procs.good();
    }

    /**
     * @brief 获取峰值内存使用量
     * @return long long 峰值内存使用量(字节)，失败返回-1
     *
     * 从memory.peak文件读取进程运行期间的峰值内存使用量
     * 这是cgroup v2提供的高精度内存监控特性
     *
     * @details memory.peak记录cgroup中所有进程的峰值物理内存使用
     *          包括程序本身、动态分配的内存、共享库等
     *          比传统的rusage.ru_maxrss更准确、更完整
     *
     * @note 只有在进程执行完毕后读取才有意义
     */
    long long getMemoryPeak()
    {
        if (!created)
            return -1;

        ifstream memory_peak(cgroup_path + "/memory.peak");
        if (!memory_peak)
            return -1;

        long long peak;
        memory_peak >> peak;
        return memory_peak.good() ? peak : -1;
    }

    /**
     * @brief 获取当前内存使用量
     * @return long long 当前内存使用量(字节)，失败返回-1
     *
     * 从memory.current文件读取当前实时内存使用量
     * 主要用于调试和实时监控
     *
     * @note 此值会实时变化，通常用于监控而非最终统计
     */
    long long getCurrentMemory()
    {
        if (!created)
            return -1;

        ifstream memory_current(cgroup_path + "/memory.current");
        if (!memory_current)
            return -1;

        long long current;
        memory_current >> current;
        return memory_current.good() ? current : -1;
    }

    /**
     * @brief 清理cgroup资源
     *
     * 删除cgroup目录，释放内核资源
     * 在析构函数中自动调用，也可手动调用
     *
     * @note 只有在cgroup中没有进程时才能成功删除
     *       进程结束后内核会自动清理，但显式删除是好习惯
     */
    void cleanup()
    {
        if (created)
        {
            // 删除cgroup目录，释放内核资源
            rmdir(cgroup_path.c_str());
            created = false;
        }
    }

    /**
     * @brief 获取cgroup名称
     * @return const string& cgroup名称的常量引用
     *
     * 返回此cgroup的唯一名称，用于调试和日志记录
     */
    const string &getName() const
    {
        return cgroup_name;
    }

    /**
     * @brief 获取分配的CPU核心编号
     * @return string 当前分配的CPU核心编号，失败返回空字符串
     *
     * 读取当前cgroup分配的CPU核心信息
     * 用于调试和验证CPU分配是否正确
     */
    string getAllocatedCpu() const
    {
        if (!created)
            return "";

        ifstream cpuset_cpus(cgroup_path + "/cpuset.cpus");
        if (!cpuset_cpus)
            return "";

        string allocated_cpu;
        getline(cpuset_cpus, allocated_cpu);
        return allocated_cpu;
    }
};

/**
 * @brief 解析JSON数字值
 * @param json JSON字符串
 * @param key 要解析的键名
 * @return long long 解析得到的数值，失败返回-1
 *
 * 简单的JSON解析器，只支持数字类型
 * 避免引入外部JSON库依赖，减少系统复杂性
 *
 * @details 解析过程：
 *          1. 查找键名位置
 *          2. 查找冒号分隔符
 *          3. 跳过空白字符
 *          4. 解析数字字符
 *
 * @note 此实现仅支持正整数，不支持负数、浮点数或科学计数法
 * @warning 输入的JSON格式必须正确，否则可能返回错误结果
 */
long long parseJsonNumber(const string &json, const string &key)
{
    // 查找键名在JSON字符串中的位置
    size_t pos = json.find("\"" + key + "\"");
    if (pos == string::npos)
        return -1;

    // 查找冒号分隔符
    pos = json.find(":", pos);
    if (pos == string::npos)
        return -1;

    // 跳过冒号后的空白字符
    pos++;
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t'))
        pos++;

    // 解析数字字符，累积计算数值
    long long result = 0;
    while (pos < json.length() && json[pos] >= '0' && json[pos] <= '9')
    {
        result = result * 10 + (json[pos] - '0');
        pos++;
    }

    return result;
}

/**
 * @brief 加载资源限制配置
 * @param limits_file 配置文件路径
 * @return Limits 解析后的限制配置结构体
 *
 * 从JSON配置文件加载各种资源限制参数
 * 支持默认值fallback，确保系统在配置文件缺失时仍能正常运行
 *
 * @details 配置项说明：
 *          - time_limit: CPU时间限制(毫秒)
 *          - memory_limit: 内存限制(KB，内部转换为字节)
 *          - output_limit: 输出大小限制(字节)
 *          - compile_timeout: 编译超时时间(毫秒)
 *          - stack_limit: 栈大小限制(KB，内部转换为字节)
 *
 * @note 所有KB单位的配置项会自动转换为字节存储
 * @warning 如果配置文件格式错误，将使用默认值并继续执行
 */
Limits loadLimits(const string &limits_file)
{
    Limits limits;
    ifstream file(limits_file);

    // 如果文件打开失败，使用默认配置
    if (!file.is_open())
    {
        // 默认配置值（适合大多数竞赛题目）
        limits.time_limit = 1000;       // 1秒
        limits.memory_limit = 67108864; // 64MB
        limits.output_limit = 64000000; // 64MB
        limits.compile_timeout = 30000; // 30秒
        limits.stack_limit = 8388608;   // 8MB
        return limits;
    }

    // 读取整个文件内容到字符串
    stringstream buffer;
    buffer << file.rdbuf();
    string json = buffer.str();

    // 解析各个配置项
    long long time_limit = parseJsonNumber(json, "time_limit");
    long long memory_limit = parseJsonNumber(json, "memory_limit");
    long long output_limit = parseJsonNumber(json, "output_limit");
    long long compile_timeout = parseJsonNumber(json, "compile_timeout");
    long long stack_limit = parseJsonNumber(json, "stack_limit");

    // 设置配置值，解析失败时使用默认值
    limits.time_limit = (time_limit > 0) ? time_limit : 1000;
    limits.memory_limit = (memory_limit > 0) ? memory_limit * 1024 : 67108864; // KB转字节
    limits.output_limit = (output_limit > 0) ? output_limit : 64000000;
    limits.compile_timeout = (compile_timeout > 0) ? compile_timeout : 30000;
    limits.stack_limit = (stack_limit > 0) ? stack_limit * 1024 : 8388608; // KB转字节

    return limits;
}

JudgeResult compileProgram(const string &source_file, const string &output_file, const Limits &limits)
{
    JudgeResult result;
    result.status = "CE";
    result.time_used = 0;
    result.mem_used = 0;
    result.exit_code = 0;
    result.output_len = 0;
    result.allocated_cpu = "";

    // 创建编译命令
    string compile_cmd = "g++ -g -std=c++20 -O2 -Wall -Wextra -Wshadow -Wconversion -Wfloat-equal " + source_file + " -o " + output_file + " 2>&1";

    auto start_time = high_resolution_clock::now();

    FILE *pipe = popen(compile_cmd.c_str(), "r");
    if (!pipe)
    {
        result.error_message = "Failed to create compilation process";
        return result;
    }

    char buffer[128];
    string compile_output;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
    {
        compile_output += buffer;
    }

    int compile_result = pclose(pipe);
    auto end_time = high_resolution_clock::now();

    result.time_used = duration_cast<milliseconds>(end_time - start_time).count();

    if (compile_result != 0)
    {
        result.error_message = compile_output;
        result.status = "CE";
        return result;
    }

    // 检查编译是否超时
    if (result.time_used > limits.compile_timeout)
    {
        result.status = "CE";
        result.error_message = "Compilation timeout";
        return result;
    }

    result.status = "OK";
    return result;
}

JudgeResult runProgram(const string &executable, const string &input_file, const Limits &limits)
{
    JudgeResult result;
    result.status = "RE";
    result.time_used = 0;
    result.mem_used = 0;
    result.exit_code = -1;
    result.output_len = 0;
    result.allocated_cpu = "";

    // 创建cgroup
    CgroupManager cgroup;
    if (!cgroup.create())
    {
        result.error_message = "Failed to create cgroup (需要root权限)";
        return result;
    }

    // 设置内存限制
    if (!cgroup.setMemoryLimit(limits.memory_limit))
    {
        result.error_message = "Failed to set memory limit in cgroup";
        return result;
    }

    // 设置CPU限制为单核心
    if (!cgroup.setCpuLimit())
    {
        result.error_message = "Failed to set CPU limit in cgroup";
        return result;
    }

    // 获取分配的CPU核心信息
    result.allocated_cpu = cgroup.getAllocatedCpu();

    // 创建管道用于获取输出
    int stdout_pipe[2];
    int stderr_pipe[2];

    if (pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1)
    {
        result.error_message = "Failed to create pipes";
        return result;
    }

    auto start_time = high_resolution_clock::now();

    pid_t pid = fork();
    if (pid == -1)
    {
        result.error_message = "Failed to fork process";
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return result;
    }

    if (pid == 0)
    {
        // 子进程

        // 重定向标准输入
        int input_fd = open(input_file.c_str(), O_RDONLY);
        if (input_fd == -1)
        {
            exit(1);
        }
        dup2(input_fd, STDIN_FILENO);
        close(input_fd);

        // 重定向标准输出和错误输出
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);

        // 设置资源限制
        struct rlimit rl;

        // 时间限制 (CPU时间)
        rl.rlim_cur = (limits.time_limit + 999) / 1000; // 向上取整到秒
        rl.rlim_max = rl.rlim_cur + 1;
        setrlimit(RLIMIT_CPU, &rl);

        // 栈限制
        rl.rlim_cur = limits.stack_limit;
        rl.rlim_max = limits.stack_limit;
        setrlimit(RLIMIT_STACK, &rl);

        // 输出限制
        rl.rlim_cur = limits.output_limit;
        rl.rlim_max = limits.output_limit;
        setrlimit(RLIMIT_FSIZE, &rl);

        // 进程数限制
        rl.rlim_cur = 1;
        rl.rlim_max = 1;
        setrlimit(RLIMIT_NPROC, &rl);

        // 执行程序
        execl(executable.c_str(), executable.c_str(), (char *)nullptr);
        exit(1); // 如果execl失败
    }
    else
    {
        // 父进程

        // 将子进程添加到cgroup
        if (!cgroup.addProcess(pid))
        {
            result.error_message = "Failed to add process to cgroup";
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
            return result;
        }

        // 强制CPU绑定到分配的核心
        string allocated_cpu_str = result.allocated_cpu;
        if (!allocated_cpu_str.empty())
        {
            int allocated_cpu_id = stoi(allocated_cpu_str);
            if (!cgroup.forceCpuBinding(pid, allocated_cpu_id))
            {
                // CPU绑定失败不中止评测，但记录警告
                result.error_message += "Warning: Failed to set CPU affinity; ";
            }
        }

        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        // 读取输出
        fd_set read_fds;
        struct timeval timeout;
        timeout.tv_sec = (limits.time_limit + 999) / 1000 + 1;
        timeout.tv_usec = 0;

        string stdout_output, stderr_output;
        char buffer[4096];
        bool stdout_done = false, stderr_done = false;

        while (!stdout_done || !stderr_done)
        {
            FD_ZERO(&read_fds);
            int max_fd = 0;

            if (!stdout_done)
            {
                FD_SET(stdout_pipe[0], &read_fds);
                max_fd = max(max_fd, stdout_pipe[0]);
            }
            if (!stderr_done)
            {
                FD_SET(stderr_pipe[0], &read_fds);
                max_fd = max(max_fd, stderr_pipe[0]);
            }

            int select_result = select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);

            if (select_result <= 0)
                break; // 超时或错误

            if (!stdout_done && FD_ISSET(stdout_pipe[0], &read_fds))
            {
                ssize_t bytes_read = read(stdout_pipe[0], buffer, sizeof(buffer) - 1);
                if (bytes_read <= 0)
                {
                    stdout_done = true;
                }
                else
                {
                    buffer[bytes_read] = '\0';
                    stdout_output += buffer;
                }
            }

            if (!stderr_done && FD_ISSET(stderr_pipe[0], &read_fds))
            {
                ssize_t bytes_read = read(stderr_pipe[0], buffer, sizeof(buffer) - 1);
                if (bytes_read <= 0)
                {
                    stderr_done = true;
                }
                else
                {
                    buffer[bytes_read] = '\0';
                    stderr_output += buffer;
                }
            }
        }

        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        // 等待子进程结束
        int status;
        struct rusage usage;
        if (wait4(pid, &status, 0, &usage) == -1)
        {
            result.error_message = "Failed to wait for child process";
            return result;
        }

        auto end_time = high_resolution_clock::now();
        result.time_used = duration_cast<milliseconds>(end_time - start_time).count();

        // 从cgroup获取峰值内存使用量
        long long memory_peak = cgroup.getMemoryPeak();

        result.mem_used = (memory_peak > 0) ? memory_peak : usage.ru_maxrss * 1024; // ru_maxrss 是 KB，需转bit

        result.stdout_content = stdout_output;
        result.output_len = stdout_output.length();

        // 判断退出状态
        if (WIFEXITED(status))
        {
            result.exit_code = WEXITSTATUS(status);
            if (result.exit_code == 0)
            {
                // 检查是否超时
                if (result.time_used > limits.time_limit)
                {
                    result.status = "TLE";
                }
                // 检查内存限制
                else if (result.mem_used > limits.memory_limit)
                {
                    result.status = "MLE";
                }
                // 检查输出限制
                else if (result.output_len > limits.output_limit)
                {
                    result.status = "OLE";
                }
                else
                {
                    result.status = "OK";
                }
            }
            else
            {
                result.status = "RE";
                result.error_message = "Program exited with non-zero code: " + to_string(result.exit_code);
                if (!stderr_output.empty())
                {
                    result.error_message += "\\nStderr: " + stderr_output;
                }
            }
        }
        else if (WIFSIGNALED(status))
        {
            int signal_num = WTERMSIG(status);
            result.exit_code = 128 + signal_num;

            switch (signal_num)
            {
            case SIGXCPU:
                result.status = "TLE";
                result.error_message = "Time limit exceeded (SIGXCPU)";
                break;
            case SIGKILL:
                // 可能是内存限制或时间限制
                if (result.mem_used > limits.memory_limit)
                {
                    result.status = "MLE";
                    result.error_message = "Memory limit exceeded (cgroup)";
                }
                else
                {
                    result.status = "TLE";
                    result.error_message = "Time limit exceeded (SIGKILL)";
                }
                break;
            case SIGSEGV:
                result.status = "RE";
                result.error_message = "Segmentation fault";
                break;
            case SIGFPE:
                result.status = "RE";
                result.error_message = "Floating point exception";
                break;
            case SIGABRT:
                // 检查是否是内存限制导致的
                if (result.mem_used > limits.memory_limit)
                {
                    result.status = "MLE";
                    result.error_message = "Memory limit exceeded (allocation failed)";
                }
                else
                {
                    result.status = "RE";
                    result.error_message = "Program aborted";
                }
                break;
            default:
                result.status = "RE";
                result.error_message = "Program terminated by signal " + to_string(signal_num);
                break;
            }
        }
    }

    return result;
}

string resultToJson(const JudgeResult &result)
{
    stringstream ss;
    ss << "{" << endl;
    ss << "  \"status\": \"" << result.status << "\"," << endl;
    ss << "  \"time_used\": " << result.time_used << "," << endl;
    ss << "  \"mem_used\": " << result.mem_used << "," << endl;
    ss << "  \"exit_code\": " << result.exit_code << "," << endl;
    ss << "  \"error_message\": \"";

    // 转义错误消息中的特殊字符
    for (char c : result.error_message)
    {
        if (c == '"')
            ss << "\\\"";
        else if (c == '\\')
            ss << "\\\\";
        else if (c == '\n')
            ss << "\\n";
        else if (c == '\r')
            ss << "\\r";
        else if (c == '\t')
            ss << "\\t";
        else
            ss << c;
    }

    ss << "\"," << endl;
    ss << "  \"stdout\": \"";

    // 转义标准输出中的特殊字符
    for (char c : result.stdout_content)
    {
        if (c == '"')
            ss << "\\\"";
        else if (c == '\\')
            ss << "\\\\";
        else if (c == '\n')
            ss << "\\n";
        else if (c == '\r')
            ss << "\\r";
        else if (c == '\t')
            ss << "\\t";
        else
            ss << c;
    }

    ss << "\"," << endl;
    ss << "  \"output_len\": " << result.output_len << "," << endl;
    ss << "  \"allocated_cpu\": \"" << result.allocated_cpu << "\"" << endl;
    ss << "}";

    return ss.str();
}

JudgeResult judge_core(const string &limits_file, const string &source_file, const string &input_file)
{
    JudgeResult result;

    try
    {
        // 加载限制配置
        Limits limits = loadLimits(limits_file);

        // 编译程序
        string executable = source_file + ".out";
        result = compileProgram(source_file, executable, limits);

        if (result.status != "OK")
        {
            return result;
        }

        // 运行程序
        result = runProgram(executable, input_file, limits);

        // 清理可执行文件
        unlink(executable.c_str());
    }
    catch (const exception &e)
    {
        result.status = "SE";
        result.error_message = "System error: " + string(e.what());
        result.time_used = 0;
        result.mem_used = 0;
        result.exit_code = -1;
        result.output_len = 0;
        result.allocated_cpu = "";
    }

    return result;
}

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        cerr << "Usage: " << argv[0] << " <limits_file> <source_file> <input_file>" << endl;
        return 1;
    }

    string limits_file = argv[1];
    string source_file = argv[2];
    string input_file = argv[3];

    JudgeResult result = judge_core(limits_file, source_file, input_file);

    cout << resultToJson(result) << endl;

    return 0;
}
