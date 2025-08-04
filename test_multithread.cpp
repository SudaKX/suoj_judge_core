#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>

using namespace std;

atomic<int> counter(0);

void worker_thread(int thread_id)
{
    // 每个线程执行计算密集型任务
    for (int i = 0; i < 1000000; ++i)
    {
        counter.fetch_add(1);
    }
    cout << "Thread " << thread_id << " finished" << endl;
}

int main()
{
    auto start = chrono::high_resolution_clock::now();

    // 尝试创建多个线程（如果CPU限制生效，这些线程将被限制在单核心上）
    vector<thread> threads;
    int num_threads = thread::hardware_concurrency();

    cout << "Creating " << num_threads << " threads" << endl;

    for (int i = 0; i < num_threads; ++i)
    {
        threads.emplace_back(worker_thread, i);
    }

    // 等待所有线程完成
    for (auto &t : threads)
    {
        t.join();
    }

    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);

    cout << "Counter value: " << counter.load() << endl;
    cout << "Execution time: " << duration.count() << " ms" << endl;
    cout << "Hardware concurrency: " << num_threads << endl;

    return 0;
}
