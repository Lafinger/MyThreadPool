#ifndef THREADPOOL_HPP
#define THREADPOOL_HPP

#include <iostream>
#include <queue>
#include <functional>
#include <list>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <future>
#include <atomic>

namespace Jason {

// to do list
// 1.if task execute overtime
// 2.atomic count instead of tasks queue is empty 
// 3.add cache thread to handle the situation which the threadpool is too busy
class ThreadPool {

private:
    // threadpool status
    int core_threads_num;
    int threads_total;
    std::atomic<int> current_threads_num {0};
    std::atomic<int> accessible_core_num {0};
    std::atomic<bool> shut_down {false};
    std::atomic<int> tasks_num {0};


    std::mutex tasks_queue_mutex;
    std::condition_variable thread_cv;

    using ThreadPtr = std::shared_ptr<std::thread>;
    using TasksQueueLock = std::unique_lock<std::mutex>;

    // thread status
    enum class ThreadType { INIT, CORE, CACHE };
    enum class ThreadState { FREE, BUSY };
    struct ThreadWrapper {
        ThreadPtr ptr = nullptr;
        ThreadType type = ThreadType::INIT;
        ThreadState state = ThreadState::FREE;
    };

    // threads list
    using ThreadWrapperPtr = std::shared_ptr<ThreadWrapper>;
    using ThreadsList = std::list<ThreadWrapperPtr>;
    ThreadsList threads_list;

    // tasks queue
    using Task = std::function<void()>;
    using TasksQueue = std::queue<Task>;
    TasksQueue tasks_queue;

    // future vector
    // using ResultVector = std::vector<std::future<>>

    void Init();

    void AddThread(ThreadType thread_type);

public:
    bool TaskIsEmpty() const;

    // ThreadPool get task to execute
    template<typename F, typename ...Args>
    auto Run(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>;

    // shut down all threads when tasks have been done
    void ShutDown();

    // shut down all threads even though tasks is not completed
    void ShutDownNow();

    // core thread : always exist thread in threadpool
    // cache thread : when core threads are busy, threadpool execute task by creating cache thread
    // cache thread number : max thread number - core thread number
    ThreadPool();
    explicit ThreadPool(int always_thread_num);
    explicit ThreadPool(int always_thread_num, int total_thread_num);
    ~ThreadPool();
};

void ThreadPool::Init()
{
    while (--core_threads_num >= 0)
    {
        AddThread(ThreadType::CORE);
        ++accessible_core_num;
    }
}

void ThreadPool::AddThread(ThreadType thread_type) 
{
    // create thread wrapper
    ThreadWrapperPtr thread_wrapper_ptr = std::make_shared<ThreadWrapper>();

    // add core thread
    if (ThreadType::CORE == thread_type) {
        // core task
        Task task_processor = [this, thread_wrapper_ptr]() {

            while (true) {

                Task task;
                thread_wrapper_ptr->state = ThreadState::FREE;

                // tasks queue lock && get a task
                {
                    TasksQueueLock u_lock(tasks_queue_mutex);

                    // waiting for task
                    thread_cv.wait(u_lock, [this, thread_wrapper_ptr]() {
                        return shut_down || !TaskIsEmpty(); 
                    });

                    if(shut_down && (TaskIsEmpty() || 0 == tasks_num)) {
                        return;
                    }

                    // get task from tasks_queue
                    task = std::move(tasks_queue.front());
                    tasks_queue.pop();
                    --tasks_num;
                }

                // execute task
                thread_wrapper_ptr->state = ThreadState::BUSY;
                --accessible_core_num;
                task(); 
                ++accessible_core_num;
            }

        };

        ThreadPtr thread_ptr = std::make_shared<std::thread>(task_processor);
        thread_wrapper_ptr->ptr = thread_ptr;
        thread_wrapper_ptr->type = thread_type;
    }
    // cache thread
    else if (ThreadType::CACHE == thread_type) {

        // cache task
        Task task_processor = [this, thread_wrapper_ptr]() {
            while (true) {

                Task task;
                thread_wrapper_ptr->state = ThreadState::FREE;

                // tasks queue lock
                {
                    TasksQueueLock u_lock(tasks_queue_mutex);

                    // waiting for task
                    thread_cv.wait(u_lock, [this, thread_wrapper_ptr]() {
                        return shut_down || !TaskIsEmpty();
                    });

                    if(shut_down || TaskIsEmpty() || (0 == tasks_num)) {
                        return;
                    }

                    // get task from tasks_queue
                    task = std::move(tasks_queue.front());
                    tasks_queue.pop();
                    --tasks_num;
                }

                thread_wrapper_ptr->state = ThreadState::BUSY;
                task(); // execute task
            }
        };

        ThreadPtr thread_ptr = std::make_shared<std::thread>(task_processor);
        thread_wrapper_ptr->ptr = thread_ptr;
        thread_wrapper_ptr->type = thread_type;

    }

    // add thread wrapper to threads list
    threads_list.push_back(thread_wrapper_ptr);
    // thread number + 1
    ++current_threads_num;
}

bool ThreadPool::TaskIsEmpty() const
{
    return tasks_queue.empty();
}

template<typename F, typename ...Args>
auto ThreadPool::Run(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F , Args...>>
{
    using TaskResultType = std::invoke_result_t<F, Args...>;

    //--------------------------push a new task to tasks_queue----------------------------
    std::shared_ptr<std::packaged_task<TaskResultType()>> task_ptr =
        std::make_shared<std::packaged_task<TaskResultType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    //std::shared_ptr<std::packaged_task<TaskResultType()>> task =
    //	std::make_shared<std::packaged_task<TaskResultType()>>([what?]() {
    //	//What do I have to do to make it the same ?
    //});

    std::future<TaskResultType> result_future = task_ptr->get_future();

    // tasks queue lock
    {
        TasksQueueLock u_lock(tasks_queue_mutex);
        tasks_queue.emplace([task_ptr]() { (*task_ptr)(); });
        tasks_num++;
    }
    //--------------------------push a new task to tasks_queue----------------------------


    // add cache thread if tasks queue is not empty and core threads is busy
    if(tasks_num && !accessible_core_num && (current_threads_num <= threads_total)) {
        std::cout << "add cache thread" << std::endl;
        AddThread(ThreadType::CACHE);
    }
    else {
        thread_cv.notify_one();
    }

    return std::move(result_future);
}

void ThreadPool::ShutDown()
{
    shut_down = true;
    thread_cv.notify_all();

    for(const auto& ptr_thread: threads_list) {
        if (ptr_thread->ptr->joinable())
            ptr_thread->ptr->join();
    }
}

void ThreadPool::ShutDownNow() 
{
    // TODO
}

ThreadPool::ThreadPool() 
{
    core_threads_num = static_cast<int>(std::thread::hardware_concurrency() / 2);
    threads_total = std::thread::hardware_concurrency();
    std::cout << core_threads_num << std::endl;
    std::cout << threads_total << std::endl;
}

ThreadPool::ThreadPool(int always_thread_num)
: core_threads_num(always_thread_num > std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : always_thread_num),
  threads_total(std::thread::hardware_concurrency()) 
{
    std::cout << core_threads_num << std::endl;
    std::cout << threads_total << std::endl;
    Init();
}

ThreadPool::ThreadPool(int always_thread_num, int total_thread_num)
: core_threads_num(always_thread_num > std::thread::hardware_concurrency() ? std::thread::hardware_concurrency(): always_thread_num),
  threads_total(total_thread_num) 
{
    std::cout << core_threads_num << std::endl;
    std::cout << threads_total << std::endl;
    Init();
}

ThreadPool::~ThreadPool() 
{
    ShutDown();
}

}

#endif //THREADPOOL_HPP
