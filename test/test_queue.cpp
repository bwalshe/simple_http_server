#include <memory>
#include <thread>
#include <catch2/catch.hpp>
#include <concurrent_queue.h>

#include <iostream>


TEST_CASE( "Pop empty queue returns null" )
{
    queue<int> queue;
    REQUIRE(!queue.try_pop());
}

TEST_CASE( "Push and pop returns same item" )
{
    queue<int> queue;
    queue.push(1);
    REQUIRE(*queue.try_pop() == 1);
}

TEST_CASE( "Single thread queue perserves order" )
{
    queue<int> queue;
    queue.push(1);
    queue.push(2);
    queue.push(3);

    REQUIRE(*queue.try_pop() == 1);
    REQUIRE(*queue.try_pop() == 2);
    REQUIRE(*queue.try_pop() == 3);
    REQUIRE(!queue.try_pop());
}

TEST_CASE( "Multiple threads can push to queue" )
{
    constexpr int elts = 1000;
    constexpr int nthreads = 100;
    bool was_in_queue[elts];
    for(auto i=0; i < elts; ++i) was_in_queue[i] = false;
    queue<int> queue;
    std::unique_ptr<std::thread> threads[nthreads];
    int block_size = elts / nthreads;
    for(int i = 0; i < nthreads; ++i)
    {
        threads[i] = std::make_unique<std::thread>([&queue, block_size, i]{
                for(int j = i * block_size; j < (i + 1) * block_size; ++j)
                { 
                    queue.push(j);
                }
            });
    }

    for(int i = 0; i< nthreads; ++i)
    {
        threads[i]->join();
    }

    while(auto i = queue.try_pop()) {
        was_in_queue[*i] = true;
    }

    for(auto i = 0; i < elts; ++i)
        REQUIRE(was_in_queue[i]);
}

TEST_CASE( "Multiple threads can pop queue" )
{
    constexpr int elts = 1000;
    constexpr int nthreads = 100;
    bool was_in_queue[elts];
    queue<int> queue;
    for(auto i=0; i < elts; ++i)
    {   
        was_in_queue[i] = false;
        queue.push(i);
    }
    std::unique_ptr<std::thread> threads[nthreads];
    int block_size = elts / nthreads;
    for(int i = 0; i < nthreads; ++i)
    {
        threads[i] = std::make_unique<std::thread>([&]{
                for(int j = 0; j < block_size; ++j)
                {  
                    was_in_queue[*queue.try_pop()] = true;
                }
            });
    }

    for(int i = 0; i< nthreads; ++i)
    {
        threads[i]->join();
    }

    for(auto i = 0; i < elts; ++i)
        REQUIRE(was_in_queue[i]);
}
