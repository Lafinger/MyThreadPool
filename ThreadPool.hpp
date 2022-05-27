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

namespace JasonChen {

    // to do list
    //  1.if task execute overtime
    //  2.atomic count instead of tasks queue is empty 
    //  3.add cache thread to handle the situation where the threadpool is too busy
    //  4. 
    class ThreadPool {

    private:
        // threadpool status
        int core_thread_num;
        int max_thread_num;
        std::atomic<bool> stop_now{false}; // shutdown

        std::mutex tasks_queue_mutex;
        std::condition_variable thread_cv;

        using ThreadPtr = std::shared_ptr<std::thread>;
        using ThreadLock = std::unique_lock<std::mutex>;

        // thread status
        enum class ThreadType { INIT, CORE, CACHE };
        enum class ThreadState { FREE, BUSY, SHUTDOWN };
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
        using TasksQueue = std::queue<std::function<void()>>;
        using Task = std::function<void()>;
        TasksQueue tasks_queue;
        
        
        void init() {
            while (--core_thread_num >= 0)
            {
                addThread(ThreadType::CORE);
            }
        }

        void addThread(ThreadType thread_type) {
            // create thread wrapper
            ThreadWrapperPtr thread_wrapper_ptr = std::make_shared<ThreadWrapper>();

            // add core thread
            if (ThreadType::CORE == thread_type) {
                
                // task
                Task task = [this, thread_wrapper_ptr] () {
                    while (true) {
                        Task task;
                        thread_wrapper_ptr->state = ThreadState::FREE;

                        // tasks queue lock
                        {
                            ThreadLock u_lock(tasks_queue_mutex);

                            // waiting for task
                            thread_cv.wait(u_lock, [this] () {
                                return stop_now || !taskIsEmpty();
                            });

                            if(stop_now && taskIsEmpty()) {
                                return;
                            }

                            // get task from tasks_queue
                            task = std::move(tasks_queue.front());
                            tasks_queue.pop();
                        }

                        thread_wrapper_ptr->state = ThreadState::BUSY;
                        // execute task
                        task();
                    }
                };

                // add core thread wrapper content
                ThreadPtr thread_ptr = std::make_shared<std::thread>(task);
                thread_wrapper_ptr->ptr = thread_ptr;
                thread_wrapper_ptr->type = thread_type;
            }
            // cache thread
            else if (ThreadType::CACHE == thread_type) {

                // task
                Task task;
                // tasks queue lock
                {
                    ThreadLock u_lock(tasks_queue_mutex);
                    if (!taskIsEmpty())
                    {
                        // get task from tasks_queue
                        task = std::move(tasks_queue.front());
                        tasks_queue.pop();
                    }
                }
                task();

                ThreadPtr thread_ptr = std::make_shared<std::thread>(task);
                thread_wrapper_ptr->ptr = thread_ptr;
                thread_wrapper_ptr->type = thread_type;
            }

            // add thread wrapper to threads list
            threads_list.push_back(thread_wrapper_ptr);
        }

    public:
        ThreadPool() {
            core_thread_num = 4;
            max_thread_num = std::thread::hardware_concurrency();
            std::cout << max_thread_num << std::endl;
        }
        // core thread : always exist thread in threadpool
        // cache thread : when core threads are busy, threadpool execute task by creating cache thread
        // cache thread number : max thread number - core thread number
        explicit ThreadPool(int always_thread_num)
            : core_thread_num(always_thread_num > std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : always_thread_num),
            max_thread_num(2 * std::thread::hardware_concurrency()) {
            std::cout << core_thread_num << std::endl;
            std::cout << max_thread_num << std::endl;
            init();
        }
        explicit ThreadPool(int always_thread_num, int total_thread_num)
            : core_thread_num(always_thread_num > std::thread::hardware_concurrency() ? std::thread::hardware_concurrency(): always_thread_num),
            max_thread_num(total_thread_num) {
            std::cout << core_thread_num << std::endl;
            std::cout << max_thread_num << std::endl;
            init();
        }
        ~ThreadPool() {
            shutDown();
        }

        bool taskIsEmpty() {
            return tasks_queue.empty();
        }

        // ThreadPool get task to execute
        template<typename F, typename ...Args>
        auto run(F&& f, Args&&... args) -> std::future<std::result_of_t<F(Args...)>> {
            using TaskResultType = std::result_of_t<F(Args...)>;

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
                ThreadLock u_lock(tasks_queue_mutex);
                tasks_queue.emplace([task_ptr]() { (*task_ptr)(); });
            }
//--------------------------push a new task to tasks_queue----------------------------

            thread_cv.notify_one();

            return std::move(result_future);
        }

        void shutDown() {
            // start shutdown all child threads
            stop_now = true;
            thread_cv.notify_all();

            // merge child threads to main thread when thread has been shutdown
            for (auto it = threads_list.begin(); it != threads_list.end(); ++it) {
                if ((*it)->ptr->joinable()) (*it)->ptr->join();
            }
        }

        void shutDownNow() {
            //TODO
        }
    };

}

#endif //THREADPOOL_HPP
