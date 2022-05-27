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
    // 1.if task execute overtime
    // 2.atomic count instead of tasks queue is empty 
    // 3.add cache thread to handle the situation where the threadpool is too busy
    // 4. 
    class ThreadPool {

    private:
        // threadpool status
        int core_thread_num;
        int max_thread_num;
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
        enum class ThreadState { FREE, BUSY};
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

        // future vector
        // using ResultVector = std::vector<std::future<>>

        void init() {
            while (--core_thread_num >= 0)
            {
                addThread(ThreadType::CORE);
                ++accessible_core_num;
            }
        }

        void addThread(ThreadType thread_type) {
            // create thread wrapper
            ThreadWrapperPtr thread_wrapper_ptr = std::make_shared<ThreadWrapper>();

            // add core thread
            if (ThreadType::CORE == thread_type) {
                // core task
                Task task = [this, thread_wrapper_ptr]() {
                    while (true) {

                        Task task;
                        thread_wrapper_ptr->state = ThreadState::FREE;

                        // tasks queue lock
                        {
                            TasksQueueLock u_lock(tasks_queue_mutex);

                            // waiting for task
                            thread_cv.wait(u_lock, [this, thread_wrapper_ptr]() {
                                return shut_down || !taskIsEmpty(); 
                            });

                            if(shut_down && (taskIsEmpty() || 0 == tasks_num)) {
                                return;
                            }

                            // get task from tasks_queue
                            task = std::move(tasks_queue.front());
                            tasks_queue.pop();
                            --tasks_num;
                        }

                        thread_wrapper_ptr->state = ThreadState::BUSY;
                        --accessible_core_num;
                        task(); // execute task
                        ++accessible_core_num;
                    }
                };

                // add core thread wrapper content
                ThreadPtr thread_ptr = std::make_shared<std::thread>(task);
                thread_wrapper_ptr->ptr = thread_ptr;
                thread_wrapper_ptr->type = thread_type;
            }
            // cache thread
            else if (ThreadType::CACHE == thread_type) {

                // cache task
                Task task = [this, thread_wrapper_ptr]() {
                    while (true) {

                        Task task;
                        thread_wrapper_ptr->state = ThreadState::FREE;

                        // tasks queue lock
                        {
                            TasksQueueLock u_lock(tasks_queue_mutex);

                            // waiting for task
                            thread_cv.wait(u_lock, [this, thread_wrapper_ptr]() {
                                return shut_down || !taskIsEmpty(); 
                            });

                            if(shut_down || taskIsEmpty() || (0 == tasks_num)) {
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

                ThreadPtr thread_ptr = std::make_shared<std::thread>(task);
                thread_wrapper_ptr->ptr = thread_ptr;
                thread_wrapper_ptr->type = thread_type;

            }

            // add thread wrapper to threads list
            threads_list.push_back(thread_wrapper_ptr);
            // thread number + 1
            ++current_threads_num;
        }

    public:
        ThreadPool() {
            core_thread_num = 5;
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
                TasksQueueLock u_lock(tasks_queue_mutex);
                tasks_queue.emplace([task_ptr]() { (*task_ptr)(); });
                tasks_num++;
            }
            //--------------------------push a new task to tasks_queue----------------------------


            // add cache thread if tasks queue is not empty and core threads is busy
            if(tasks_num && !accessible_core_num && (current_threads_num <= max_thread_num)) {
                std::cout << "add cache thread" << std::endl;
                addThread(ThreadType::CACHE);
            }
            else {
                thread_cv.notify_one();
            }

            return std::move(result_future);
        }

        // shut down when tasks have been done
        void shutDown() {
            shut_down = true;
            thread_cv.notify_all();
            for (auto it = threads_list.begin(); it != threads_list.end(); ++it) {
                if ((*it)->ptr->joinable()) (*it)->ptr->join();
            }
        }

        void shutDownNow() {
            // TODO
        }
    };

}

#endif //THREADPOOL_HPP
