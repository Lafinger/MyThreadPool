#include "ThreadPool.hpp"

int main()
{
    JasonChen::ThreadPool my_thread_pool(4);

    std::function<int(int, int)> task = [](int a, int b)->int {
        int result = a + b;
        std::this_thread::sleep_for(std::chrono::seconds(3));

        return a + b;
    };

    std::future<int> tasks_result_vec[10];

    for (int i = 0; i < 10; ++i) {
        // std::cout << "task finished : " << my_thread_pool.run(task, i, i + 1) << std::endl;
        tasks_result_vec[i] = my_thread_pool.run(task, i, i + 1);
    }


     for (int i = 0; i < 10; ++i) {
         std::cout << tasks_result_vec[i].get() << std::endl;
     }
    
    my_thread_pool.shutDown();

    return 0;
}