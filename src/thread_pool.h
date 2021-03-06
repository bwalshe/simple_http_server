#include <condition_variable>
#include <future>
#include <mutex>
#include <vector>
#include <thread>
#include "concurrent_queue.h"
#include "util.h"


template <class R>
class ThreadPool
{
    std::vector<std::thread> m_workers;
    queue<std::packaged_task<R(void)>> m_tasks;
    std::mutex m_empty_queue_mtx;
    std::condition_variable m_empty_queue_cv;
    std::atomic<bool> m_alive;

    void run()
    {
        block_signals();
        while(m_alive)
        {
            auto current_task = m_tasks.try_pop();
            if(!current_task)
            {
                std::unique_lock<std::mutex> lck(m_empty_queue_mtx);
                m_empty_queue_cv.wait(lck, [&] {
                        current_task = m_tasks.try_pop();
                        return static_cast<bool>(current_task) || !m_alive;
                });
            }
            if(current_task)
            {
                (*current_task)();
            }
        }
    }
 
public:
    ThreadPool(size_t n_threads = std::thread::hardware_concurrency()): 
        m_workers(n_threads),
        m_alive(true)
    {
        for(auto i = 0ul; i < n_threads; ++i)
        {
            m_workers.push_back(std::thread(&ThreadPool::run, this));
        }
    }

    ~ThreadPool()
    {
        shutdown();
    }

    template  <class Function>
    std::future<R> submit(Function &&f)
    {
        std::packaged_task<R(void)> task(f);
        auto future = task.get_future();
        m_tasks.push(std::move(task));
        m_empty_queue_cv.notify_one();
        return future;
    }

    bool shutdown()
    {
        m_alive = false;
        m_empty_queue_cv.notify_all();
        for(auto &worker: m_workers)
        {
            if(worker.joinable())
            {
                worker.join();
            }
        }
        return true;
    }
};
