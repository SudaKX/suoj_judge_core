#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include <unistd.h>
#include <sched.h>

using namespace std;

int getCurrentCpu()
{
    return sched_getcpu();
}

void showCpuAffinity()
{
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);

    if (sched_getaffinity(getpid(), sizeof(cpu_set), &cpu_set) == 0)
    {
        cout << "允许使用的CPU核心: ";
        int count = 0;
        for (int i = 0; i < CPU_SETSIZE; ++i)
        {
            if (CPU_ISSET(i, &cpu_set))
            {
                cout << i << " ";
                count++;
            }
        }
        cout << "(共" << count << "个核心)" << endl;

        if (count == 1)
        {
            cout << "✓ 程序被严格绑定到单个CPU核心" << endl;
        }
        else
        {
            cout << "⚠ 警告: 程序可以使用多个CPU核心" << endl;
        }
    }
}

bool setCpuAffinity(int cpu)
{
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    CPU_SET(cpu, &cpu_set);

    if (sched_setaffinity(getpid(), sizeof(cpu_set), &cpu_set) == 0)
    {
        cout << "✓ 强制设置CPU亲和性到核心 " << cpu << " 成功" << endl;
        return true;
    }
    else
    {
        cout << "⚠ 设置CPU亲和性失败" << endl;
        return false;
    }
}
int main()
{

    cout << "=== CPU核心绑定验证程序 ===" << endl;
    cout << "进程PID: " << getpid() << endl;

    // 显示CPU亲和性
    showCpuAffinity();

    // 显示当前运行的CPU
    int initial_cpu = getCurrentCpu();
    cout << "初始运行在CPU: " << initial_cpu << endl;

    // 强制设置CPU亲和性到当前CPU核心
    setCpuAffinity(initial_cpu);

    // 再次确认CPU亲和性设置
    cout << "重新确认CPU亲和性:" << endl;
    showCpuAffinity();

    cout << "" << endl;

    // 进行计算并监控CPU核心使用
    cout << "开始CPU密集计算，监控核心绑定情况..." << endl;
    auto start = chrono::high_resolution_clock::now();

    volatile long long sum = 0;
    int cpu_switch_count = 0;
    int last_cpu = initial_cpu;
    long long kk = 0;
    for (long long i = 0; i < 20000000LL; ++i)
    {
        sum += i;
        kk += random() % 100; // 西西
        kk -= random() % 50;
        // 每隔一定间隔检查CPU核心
        if (i % 19 == 0)
        {
            int current_cpu = getCurrentCpu();
            // cout << "进度: " << (i / 200000) << "%, 当前CPU: " << current_cpu;

            if (current_cpu != last_cpu)
            {
                cpu_switch_count++;
                cout << " [CPU切换!]";
                cout << endl;
            }

            last_cpu = current_cpu;
        }
    }

    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);

    cout << "" << endl;
    cout << "=== 计算完成 ===" << endl;
    cout << "最终结果: " << sum << endl;
    cout << "执行时间: " << duration.count() << " ms" << endl;
    cout << "最终运行在CPU: " << getCurrentCpu() << endl;
    cout << "CPU切换次数: " << cpu_switch_count << endl;

    if (cpu_switch_count == 0)
    {
        cout << "✓ 程序严格运行在单个CPU核心，无切换" << endl;
    }
    else
    {
        // cout << "⚠ 检测到CPU核心切换，绑定可能不够严格" << endl;
        throw std::runtime_error("CPU核心绑定不严格，检测到切换");
    }

    cout << "" << endl;
    cout << "注意：评测结果的 'allocated_cpu' 字段将显示分配的CPU核心编号" << endl;

    return 0;
}
