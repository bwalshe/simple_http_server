#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
#include "concurrent_queue.h"
#include "util.h"


template <size_t N, class R>
class ThreadPool
{
    struct Worker
    {
        int name;
        ThreadPool *pool;
        std::atomic<bool> alive;
        std::thread thread;
        void run()
        {
           thread = std::thread([&]{
                block_signals();
                while(alive)
                {
                    auto current_task = pool->tasks.try_pop();
                    if(!current_task)
                    {
                        std::unique_lock<std::mutex> lck(pool->wait_mtx);
                        pool->wait_condition.wait(lck, [&]() -> bool {
                                current_task = pool->tasks.try_pop();
                                return static_cast<bool>(current_task) || !alive;
                                });
                    }
                    if(current_task)
                    {
                        (*current_task)();
                    }
                }
            });
        }
    };

    Worker workers[N];
    queue<std::packaged_task<R(void)>> tasks;
    std::mutex wait_mtx;
    std::condition_variable wait_condition;

public:
    ThreadPool()
    {
        for(size_t i = 0; i < N; ++i)
        {
            workers[i].pool = this;
            workers[i].name = i;
            workers[i].alive = true;
            workers[i].run();
        }
    }

    ~ThreadPool()
    {
        shutdown();
    }

    std::future<R> submit(std::function<R(void)> &&f)
    {
        std::packaged_task<R(void)> task(f);
        auto future = task.get_future();
        tasks.push(std::move(task));
        wait_condition.notify_one();
        return future;
    }

    bool shutdown()
    {
        for(auto &worker: workers)
        {
            worker.alive = false;
        }
        wait_condition.notify_all();
        for(auto &worker: workers)
        {
            if(worker.thread.joinable())
            {
                worker.thread.join();
            }
        }
        return true;
    }
};
