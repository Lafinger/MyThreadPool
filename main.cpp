#include "ThreadPool.hpp"

#include <chrono>

int main()
{
    // customize tasks
    auto task = [](int a, int b) -> decltype(auto) {
        int result = a + b;
        // std::this_thread::sleep_for(std::chrono::seconds(3));
        return a + b;
    };

    // test
    auto start_time = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000000; ++i) {
        task(i, i + 1);
    }

    std::cout << "direct processing time : " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count()  << std::endl;

    Jason::ThreadPool my_thread_pool(4);

    std::vector<std::future<int>> tasks_results;

    // std::chrono::time_point<std::chrono::high_resolution_clock> start_time = std::chrono::high_resolution_clock::now();
    start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 1000000; ++i) {

        tasks_results.emplace_back(my_thread_pool.Run(task, i, i + 1));
    }

    std::cout << "thread pool time : " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count()  << std::endl;

    //  for(auto& result : tasks_results) {
    //     std::cout << result.get() << std::endl;
    //  }

    my_thread_pool.ShutDown();

    return 0;
}