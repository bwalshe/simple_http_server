#include <catch2/catch.hpp>
#include <thread>
#include <thread_pool.h>

TEST_CASE( "Thread pool starts up and shuts down" )
{
    ThreadPool<int> pool(10);
    REQUIRE(pool.shutdown());
}

TEST_CASE( "Thread pool executes a single packaged task" )
{
    ThreadPool<int> pool(1);
    auto result = pool.submit([]{return 1;});
    REQUIRE(result.get() == 1);
}


TEST_CASE( "Thread pool executes all tasks" )
{
    constexpr int ntasks = 1000;
    ThreadPool<int> pool(100);
    std::future<int> results[ntasks];
    bool task_completed[ntasks];
    for(bool &t: task_completed) t=false;
    for(auto i = 0; i < ntasks; ++i)
    {
        results[i] = pool.submit([=]{return i;});
    }

    for(auto &r: results) 
        task_completed[r.get()] = true;
    
    for(bool completed: task_completed) 
        REQUIRE(completed);
}
