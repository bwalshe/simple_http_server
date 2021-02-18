# Building A Simple(ish) HTTP Server with Asynchronous IO from Scratch

I work in a company that produces a real-time, machine learning, fraud
detection product. It seems *real-time* is a bit of a hot topic in machine 
learning at the moment and I've noticed an uptick in blog posts and Medium
articles on the subject. Being machine learning articles, they tend to
focus on the machine learning specific problems with real-time learning - 
problems such as how to update your model in response to 
incoming information and how to monitor the behaviour of the model to spot any
potential issues. These articles tend to gloss over the more fundamental 
requirement of a real time decisioning system - it has to produce its results
in real time. 

The phrase "real time" gets thrown around a lot, but it has a very specific meaning -
*real-time* means that the operation has a deadline. In a real time service, 
clients will connect to the system, send a request, and expect a response back 
on the same connection within X milliseconds. If it takes any longer, then the 
service operator will probably be in breach of an SLA, and could face some kind 
of financial penalty. In the payments industry our SLAs can mean that we have 
just milliseconds to produce a response, and that includes all the time it takes
to get the data in and out of the system - not just the time our model spends 
calculating a score.

<img align="right" src="docs/covid_homer.jpg" alt="Christmas 2020"/>

As a machine learning engineer, I don't have to deal with this problem directly.
I mostly work on offline tools used by our data scientists to build and test
their models. For tools like this, I care a lot more about improving overall
throughput than latency. If I build anything that *does* have to work with
our real time system, then I am given a set of constraints to follow - such as
not accessing disk directly, not using too much CPU time - and as long as I 
follow these constraints, someone else worries about making sure that this all 
works. 

Until recently, when it came to real-time decisioning, I was much more 
interested in the decisioning part than the real-time part. That was until 
Christmas 2020. I went home to see my family and two days later I got a text 
message from the Irish Government saying that I had to remain in  my room for 
the next two weeks. I couldn't find anything good on Netflix, so I decided to
crack open my dusty copy of 
[C++ Concurrency in Action](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition)
and try to get to grip with this real time stuff.

I've always been interested in how HTTP servers such as Nginx and Node work.
HTTP servers are not strictly Real Time Systems, as there is no hard limit on 
their response time, but they are often vital components in an RTS.
In general, they need respond to many concurrent connections with as low a latency 
as possible. Even if there is no SLA in place, people have come to expect that 
web servers should produce results in a timely manner.

Both Nginx and Node use non-blocking IO to achieve their levels of performance.
In this repo I have attempted to develop a non-blocking web-server of my own, using
as few libraries as possible. Ideally I would have liked to have done this using
just the C POSIX Library and the standard C++ headers, as my goal was to make sure
that I understood the process fully. In the end I did use someone else's unit testing
framework and a concurrent map implementation, but other than that everything is 
implemented from scratch. I had intended for this article to become a how-to, 
something that gentl guided the reader through the implementation. Unofrtuneately
there is a lot to get through and not all of it is intersting. Instead this has
become more of a stream-of-conciousness description of the things I learned 
while implementing the server. There are a few jumps, but I hope the individual 
sections make sense.

Before we go on, I should re-iterate that I do not work on real-time stuff
professionally. I am not suggesting that this is a great implementation, and
you should not use this as a reference on how to use non-blocking 
IO. On top of this, I have had no involvement in the development of the real-time 
component of Featurespace's products, and so this is not representative of how 
any of that works. This code has not been through a review, I rarely use C++, 
and it is the first time I have written a threaded C++ program - so *caveat
emptor*.

## Implementing the most basic web server I can think of

In it's simplest form, a web server uses the following loop:
 1. Wait for a connection
 2. Read the request data from the connection
 3. Use that process the request and produce a response
 4. Send the response back down the connection

It's pretty simple. I made a diagram, but it's hardly needed.

![Simple HTTP loop](docs/simple_http_loop.svg)

If you wanted to implement this, you'd need is something like the following:

```c
void process_request(char * in, size_t in_len, char * out);

void run()
{
    char in_buffer[BUFFER_SIZE]; // A buffer for the incoming data
    char out_buffer[BUFFER_SIZE]; // and for the outgoing data


    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    /* 
    Then some cryptic incantations to bind the socket to a port and put it in 
    listen mode. Also, each call to socket(), accept(), read(), etc. should 
    have a check that there was no error, but that would make this example very
    verbose.
    */

    struct sockaddr_in conn_addr; // This will hold the incoming address, but
                                  // note that I don't actually use it for 
                                  // anything.
    socklen_t conn_addr_len = sizeof(conn_addr);
    size_r request_len;
    size_t response_len;
    for(;;)
    {
        int conn_fd = accept(sock_fd, (struct sockaddr *)&conn_addr, &conn_addr_len);
        request_len = read(conn_fd, in_buffer, sizeof(buffer) * BUFFER_SIZER);
        response_len = process_request(in_buffer, request_len, out_buffer);
        write(con_fd, out_buffer, response_len);
        close(con_fd);
    }
}
```

It's not the most robust solution, but I *think* it should do the job. There is 
a bit of setup that I left out, but you should be able to find the details in 
the Linux man pages somewhere. You can check out my code 
[here](https://github.com/bwalshe/simple_http_server/blob/main/src/connection.cpp#L63)
to get an idea of how it works. 

All the action happens in that infinite `for` loop. First of all we wait for a 
connection using the `accept()` function. This returns a file descriptor and 
writes an address into the `conn_addr` variable. The address tells us where the
connection is coming from but we don't really use this - all we care about is 
the file descriptor. Which lets us write to the connection just like it was a
regular file.

The similarity of writing to a file and a connection seems a bit strange at 
first, but it makes more sense when you understand how IO is organised in Linux.
IO actions, such as reading and writing, are carried out by the kernel which
has low-level access to all the hardware in the system. Our code lives in a more
restricted area known as userland. The methods `read()` and `write()` are API calls 
that allow our userland code to communicate with the kernel, telling it where we want it
to send data. This means that the userland code doesn't really know anything about 
files, sockets or connections, all it knows is that it can tell the kernel to send
and receive data - the same API is used regardless of where the data is being sent.

To process a request, first the kernel will read a bunch of bytes from the 
connection and put them in a buffer where the userland code can access it. In
userland, we process the data in the buffer and write the result into another
buffer using the `process_request()`, and then get the kernel to writing that 
buffer back to the connection. When we are done, we close the connection which 
signals to the client that no more data is coming. On the users' side they are 
going to see their loading icon spin from the moment the connection is created,
right up until the moment `close()` returns.

This implementation has quite a few shortcomings. It's not very realistic to 
expect the requests and responses to always fit in a
fixed size buffer, so in reality the web server would probably have to do 
several reads and writes to the connection to complete a request. I'm going to 
gloss over that and assume that all requests can be served by reading once, 
writing once and then closing the connection. Another problem is that it's not
capable of serving more than one connection at a time, but this is something 
we can fix pretty easily.


## Serving more than one connection at a time
Serving one request at a time is not very practical. If we were serving up web
pages, then our users would not see any results until everyone ahead of them
gets served. To each of the individual users it would look like the site was 
broken and they would probably hit refresh, generating a new request which 
makes things even worse. If the server was supporting a real time system, then 
the effect would be even more disastrous. One slow request could cause all the
requests that come after it to be delayed - instead of missing our SLA on one 
request, we would end up missing it on **every** request. Even if we can engineer 
`process_request()` to complete in a split second, we can never guarantee that
`read()` and `write()` will be quick, as they are transferring data over a network
so they are outside of our control. The simple solution to this is pretty 
straightforward: for each new connection spin up a thread to do the 
read/process/write steps. If one of the requests ends up being slow, it's fine,
the others will be unaffected.

![Using threads to serve requests](docs/threaded_http.svg)

Spawning a thread in `C` is a kind of awkward, and can involve scary `(void *)`
casts, but in `C++` it's pretty clean. You can use 
[`std::thread`](https://en.cppreference.com/w/cpp/thread/thread) to run a 
function that doesn't return anything, or
[`std::async`](https://en.cppreference.com/w/cpp/thread/async) to run a 
function and get the result packaged in an 
[`std::future`](https://en.cppreference.com/w/cpp/thread/future). I'm not going
to code up the simple multi threaded server in this section, but it would look 
pretty similar to the one above, only with the read/write/close part wrapped up 
in a function `void process_connection(int conn_fd)`, and the main loop would become

```c++
for(;;)
{
    int conn_fd = accept(sock_fd, (sockaddr *)&conn_addr, &conn_addr_len);
    std::thread t(&proces_connection, conn_fd);
    t.detatch();
}
```

This will work pretty well in a lot of cases, but it still has a few issues.
First of all, spawning a thread is not free and is going to introduce some 
latency. Secondly there is no upper limit on how many threads this thing
will try to spawn at once. I'm not super worried about the latency, as Linux
thread creation is fairly snappy, but the unbounded thread creation is scary.
If 10,000 clients all start sending requests at the same time, we could have
100k+ threads all vying for the same resources, and that is probably not going
to go well.

![We call it, "Three Stooges Syndrome"](docs/3_stooges_syndrome.png)


## Limiting the number of concurrent threads

Instead of spawning a new thread for each connection as needed, we could instead spawn a
fixed number of worker threads which live for the entire lifetime of our 
server. Each time a new connection comes in, we could pass it to one of 
these workers and let it read from the connection and send a response. This 
would eliminate the pause while we wait for a thread to be created, and it would 
prevent us creating too many threads at once and jamming up the system.

This would introduce two new problems - how do we distribute the work between
each of the treads fairly, and what do we do if we get a new connection while
all the threads are busy? One of the simplest solutions is to put the connections
on a queue and have the workers grab them when they are ready. This way, the 
connections automatically go to whichever worker thread is not busy processing
a request, and if there are more requests than workers, the extra connections 
get buffered on the queue.
 
If we did that our main loop would be something like this:

**In our "main" thread**
 1. Wait for a connection 
 2. Put the connection on a queue 

**On one of the "worker" threads**
 1. Pull an awaiting connection off the queue
 2. Read data from the connection
 3. Generate a response
 4. Send the data

![](docs/thread_queue.svg)


This is a pretty standard pattern in concurrent programming known as a **Thread
Pool**. Thread pools are more general purpose than this of course - instead of putting connections
on the queue, we put generic *jobs* on there, and the thread pool executes 
whatever is in the job instead of reading a connection and generating a response.

If we were using a JVM language then we could just use the built in method 
[Executors.fixedThreadPool(int n)](https://docs.oracle.com/en/java/javase/11/docs/api/java.base/java/util/concurrent/Executors.html#newFixedThreadPool(int)) 
which will give us an 
[ExecutorService](https://docs.oracle.com/en/java/javase/11/docs/api/java.base/java/util/concurrent/ExecutorService.html)
object that we can submit jobs to. Submitting a job to a Java `ExecutorService`
is a bit like using `std::async`, except it won't always spin up a new thread
for you. In the case of a thread pool backed `ExecutorService` it has a finite
number of threads that are already running, and it will use these to execute the
job..

Unfortunately, there is no thread pool implementation built in to the standard
C++ libraries. I thought about using the 
 [Intel Thread Building Blocks](https://github.com/oneapi-src/oneTBB),
but it seems that this library was created before C++14 and they are still in 
the process of adapting to the new standard, meaning that it doesn't behave
well with [std::future](https://en.cppreference.com/w/cpp/thread/future) and 
[std::packaged_task](https://en.cppreference.com/w/cpp/thread/packaged_task). 
Futures and packaged tasks are going to become important in a later section when
we get to non-blocking IO, so I decided to abandon TBB and just make my own 
threadpool. Luckily I had seen something really similar in the last chapter of 
[The Rust Programming Language](https://doc.rust-lang.org/book/ch20-02-multithreaded.html)
by Klabnik and Nichols, so I knew it would be straightforward enough. 

Let's look at the thread pool implementation in
[thread_pool.h](https://github.com/bwalshe/simple_http_server/blob/main/src/thread_pool.h)
The thread pool is designed to take functions (which it will execute at some 
point in the future) and give us `std::future` objects which it will hold the
results of executing the function.

### Using the Thread Pool
When constructing the thread pool, we have to say up front what the return type
of the functions we will be using is, as well as how many threads we want to 
assign to the pool. The thread pool then gives us the method:
```
template  <class Function>
std::future<R> ThreadPool::submit(Function &&f)
```
This method will accept any function and its arguments, as long as the return
type for that function is compatible with the one we specified when creating the 
pool. Putting the result of running `f` allows us to get this result at a later
time, possibly in another thread. `std::future` is designed to make it safe to
pass values between threads, with the only caveats being that you can only read
the value once, and if you try and read it before it is ready, the call will 
block until the value becomes available. 

```c++
// set up all the sockets, etc as in the previous example

ThreadPool<void> thread_pool(10);

for(;;)
{
    int conn_fd = accept(sock_fd, (sockaddr *)&conn_addr, &conn_addr_len);
    thread_pool.submit([=] {process_connection(conn_fd);});
}
```
The lambda we passed to `ThreadPool::sumbit` doesn't return anything so we 
didn't use the futures this time. These will become important later on when
we start using non-blocking IO.

### Thread Pool Implementation Details

The main components are `m_queue`, the queue of packaged tasks, and 
`m_workers`, a vector of threads that pop these tasks off the queue and run 
them. I'll explain packaged tasks a bit later, but basically they are just a
wrapper around the functions we submit to the thread pool, which make them 
easier to manage.

The workers all run the same `ThreadPool::run()` function. This function starts
by suppressing the `SIGINT` and `SIGQUIT` signals and then goes into a loop 
trying to pop tasks off the  queue and execute them. Suppressing `SIGINT` and
`SIGQUIT` isn't strictly necessary, but it will allow us to shut the thread
pool down gracefully if the user hits *Ctrl-C* while the pool is running. Doing
this does mean that we have to be careful to always shut down the threads in
the pool when we are done with it, or our program will never terminate. 

```C++
void ThreadPool::run()
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
```

The loop keeps running until `m_alive` is set to false. This gives us a way of 
shutting everything down when we are done - we set `m_alive = false` and wait 
for all the threads to finish looping. The queue is a special concurrent queue
that I copied almost verbatim from C++ Concurrency in action, except that it 
uses `std::unique_ptr` to point to its elements instead of a `std::shared_ptr`.
It won't block if it's empty and you try to pop a value from it, instead it will just
return an empty pointer. This means that after executing `auto current_task = m_tasks.try_pop()`
we  have to check if `current_task` is empty. If it is then we need to get the 
thread to go to sleep until either an element is put on the queue or `m_alive`
is set to false. 

Getting the worker thread to go to sleep and wait for a value to be added to
the queue is done using a
[`std::condition_variable`](https://en.cppreference.com/w/cpp/thread/condition_variable) 
called `m_empty_queue_cv`. The condition variable provides the method 
`void wait( std::unique_lock<std::mutex>& lock, Predicate pred )` that puts the 
current thread to sleep until some other thread calls `notify_one()` or `notify_all()`
on the variable. When this happens the thread will wake up and execute `pred` and
go back to sleep if it does not return true. Each time it is woken up, it will run
`pred`, until eventually `pred` returns true and the thread's execution moves
on to the next line.

In our case, the wait predicate is a bit complicated and has a side effect. 
Side effects are best avoided, but this one is fairly well contained. The wait
predicate first tries to pop a value form `m_tasks` and assign it to 
`current_task`. Then it checks  if either `current_task` now contains a 
non-null value, or of `m_alive` has become false - either it's managed to pick
up a task from the queue, or the pool has been shut down so it's time to stop 
waiting around. Once the thread has finished waiting, we do another check to
see if the task is non-null (we might have stopped waiting without picking up a
task because `m_alive` is false.)

Putting a task onto the queue is a bit more straightforward, the only tricky bit
is making sure that any results produced by the task are accessible in some way.
This is done using 
[`std::packaged_task<T>`](https://en.cppreference.com/w/cpp/thread/packaged_task) 
\- a class that wraps functors and 
provides a `std::future<T>` where the return value of the functor will be 
stored when it is eventually run. The submit function is implemented as follows.

```
template  <class Function>
std::future<R> ThreadPool::submit(Function &&f)
{
    std::packaged_task<R(void)> task(f);
    auto future = task.get_future();
    m_tasks.push(std::move(task));
    m_empty_queue_cv.notify_one();
    return future;
}
```

First we wrap the function in a task and get the future associated with the 
task. We then move the task onto the task queue, and call `notify_one()` on the
wait condition, in case all the workers are stuck waiting for the queue to fill.
Finally we return the future so that whoever submitted the task has some way 
of getting the result when it is ready.

The last part of the puzzle is making sure that the worker threads all shut 
down when we are done with the thread pool. To do this we have a `shutdown()`
method which sets `m_alive` to  false, calls `m_empty_queue_cv.notify_all()`
to wake up any threads that are waiting on a task, and waits for each of the 
threads to see  `m_alive` has become false and finish what they are doing.
For good measure, we will add a call to `shutdown()` to the `ThreadPool`
destructor, so that it will be automatically called when the thread pool is 
cleaned up.

## Event driven, Non-blocking IO

Now we get to the interesting part. As there are a limited number of threads
available, it is important that when each thread is active it spends its time
doing important work and not just waiting for something to happen. Right now
tough, this is not the case. When responding to a new request the thread 
carries out the following steps. 

    1. Read the request data from the socket and into a local buffer.
    2. Process the request and produce a response.
    3. Write the response data back out to the socket. 

### Non-blocking IO

Steps 1 and 3 involve transferring data over a network, which is an extremely
slow operation. The worst part is that our thread isn't really doing anything 
while this data transfer is happening. This is because our thread is collection
of actions that take place in *userland*, and when it calls `read()` or `write()` 
it is making an API call to the kernel which will perform actions that 
are scheduled in a different way to the operations that are part of the thread. 
The standard behaviour for IO actions - which is known as blocking mode - is 
for the thread to sit and wait for the call to 
`read/write` to finish before carrying on. For network connections, this is 
pretty wasteful, as a call to `read()` will likely have to wait quite a bit of time
for the data to be sent across the network before the kernel can copy it into
a buffer allocated to the thread. Similarly for writes, copying the buffer out
to the kernel is pretty quick, but it is going to take a long time for the 
kernel to send that data out across the network. The diagram below shows what 
that looks like for a simple HTTP request. You can see that the thread spends
most of its time just waiting for the IO to complete, and relatively little
time actually processing the request and producing a response.

![](docs/blocking_io_session.svg)

To alleviate this problem, we can tell the kernel that we want to use 
*non-blocking* mode. When this mode is enabled, reads and writes will return
straight away, but if we try and read or write when the socket isn't ready then
we get an error. We could also end up closing a connection before a 
non-blocking write has finished and end up losing the unsent data. Things 
become more complicated, but if we had a way of timing the reads and writes so
that we do them as soon as possible without getting an error, then we could 
potentially deal with the requests in a much more efficient manner, as is shown
in the figure below. The overall time taken to serve an individual request
doesn't change, but the amount of time the thread is busy with that request is
much shorter, so it can get on to serving a new request much more quickly.

![](docs/non_blocking_io_session.svg)

In addition to timing the reads and writes correctly, we also need to make sure
that `close()` is called on the connection once the data has finished sending.
The worker thread can't do this itself, as it will have moved on to processing
a new request before the data transfer has finished. In order to do all these actions 
correctly, we need to listen for operating system events which will tell us when
the sockets are ready for each specific action.

### Listening for OS Events

Instead of guessing when the OS is going to be ready for IO events to happen, 
we can create a watch-list and have the OS tell us when the condition we are
waiting for is ready. This is done using 
[`epoll`](https://man7.org/linux/man-pages/man7/epoll.7.html)

When using `epoll` we create a list of file descriptors called the *intrest* 
list, and then `epoll` gives us an efficient way of searching this list for
file descriptors that are ready for IO actions. The epoll instance is created
using [`epoll_create`](https://man7.org/linux/man-pages/man2/epoll_create.2.html)
and file descriptors are added to the list using 
[`epoll_ctl`](https://man7.org/linux/man-pages/man2/epoll_ctl.2.html). When we
add file descriptors to the interest list, we also need to say what kind of 
events we are interested in. The three types we will need for our web-server 
are:

* **EPOLLIN** - the file descriptor is ready to read.
* **EPOLLOUT** - the file descriptor is ready to be written to
* **EPOLLRDHUP** - the client has closed the connection

When our application needs to know what events have occurred, we use 
[`epoll_wait`](https://man7.org/linux/man-pages/man2/epoll_wait.2.html)
a function which waits until it either receives some events or until a
timer runs out. 

Our server will constantly update the list and then wait for events, performing
what ever IO actions are appropriate whenever it gets an event. The [man page for
`epoll`](https://man7.org/linux/man-pages/man7/epoll.7.html) has a pretty good 
example of its use, which you should check out.  

The basic process our server uses is as follows.

1. Create a file descriptor for the socket which will listen for incoming
connections using `socket(AF_INET, SOCK_STREAM, 0)`, just like we did for the 
single threaded server example, but now we put the FD in non-blocking mode 
using `fcntl(fd, F_SETFL, O_NONBLOCK)`. 
2. Add a watch for EPOLLIN events on this socket socket FD to the epoll list 
using `epoll_ctl`.
3. When get get an EPOLLIN on the socket then it means there is a connection
waiting. We create a new file descriptor for the incoming connection, set it
to non-blocking, and add an EPOLLIN watch for this FD to the epoll list. 
4. Wait for the next event. If it is an EPOLLIN on the socket FD, then we
go back to step 3
5. If the event is an EPOLLIN on one of the connection FDs then that connection
has data waiting for us. We read a request from the data and spawn a thread to 
that will create a response to the thread and then add a watch for EPOLLOUT
events on the connection's FD.
6. We wait for another event. Eventually an EPOLLOUT will come for one of the 
connections that we have produced a response for. We take the prepared response
and send it.
7. The next time we get an EPOLLOUT for the connection we just served, we know 
that the data has finished sending. We close the connection and remove the FD
from the epoll interest list.


The event based nature of the loop means that one conductor thread can handle many 
connections concurrently as it is just updating the epoll list and triggering
worker threads that do the heavy lifting.

One challenge is keeping track of responses we have produced but have not sent 
out yet. This is done using a `map<int, std::future<Response>>` object which 
maps from file descriptors (which are just `int`s) to the awaiting responses.
The responses need to be wrapped in a `std::future` as they will be generated 
in a worker thread, but written to the connection by the conductor thread. If 
were to have the conductor thread access the response before the worker had 
finished preparing it, it would cause some strange behaviour. By wrapping the
response in a future, the worst that can happen if the conductor tries to 
access the response too soon, is that the thread will be blocked until the
response is ready. Of course this should never happen, as the worker  won't add 
the corresponding EPOLLOUT watch to the epoll list until after it has finished
producing the response. 

In order to prevent a bottle neck when reading and writing responses to the 
response map, we need to use a map that can safely be read and updated by 
multiple threads at once. In Java we could use the built in 
[ConcurrentHashMap](https://docs.oracle.com/javase/8/docs/api/java/util/concurrent/ConcurrentHashMap.html),
but unfortunately  the standard libraries in C++ do not have anything like this.
Implementing a good concurrent map - one which would give better performance
than just using `std::unordered_map` with a mutex to prevent concurrent access -
is not easy, and this article is already much longer than I was planning, so I
decided to just use an existing one. Thread Building Blocks has
[this implementation](https://www.threadingbuildingblocks.org/docs/help/tbb_userguide/concurrent_hash_map.html)
which looks pretty good, so I went with that. 

The code which handles events for the HTTP server is contained in the 
method [`TcpConnectionQueue::handle_connections()`](https://github.com/bwalshe/simple_http_server/blob/main/src/connection.cpp#L211)
defined in `connection.cpp`. The full listing for that method is as follows:

```C++
std::vector<TcpConnectionQueue::connection_ptr> TcpConnectionQueue::handle_connections(int timeout_ms)
{
    std::vector<TcpConnectionQueue::connection_ptr> connections;

    int nfds = throw_on_err(
            epoll_wait(m_epoll_fd, m_epoll_buffer, m_max_batch_size, timeout_ms),
            "epoll_wait");
    for(auto i = 0; i < nfds; ++i)
    {
        int event_type = m_epoll_buffer[i].events;
        int event_fd = m_epoll_buffer[i].data.fd;

        if(event_fd == m_sig_fd)
        {
            shutdown();
        }
        else if (event_fd == m_sock_fd)
        {
            accept_connection(m_sock_fd, m_epoll_fd);
        }
        else if(event_type & EPOLLIN)
        {
            throw_on_err(epoll_delete(m_epoll_fd, event_fd),
                    "Remove incoming connection from epoll");
            connections.push_back(
                    connection_ptr(new IncomingConnection(event_fd, this));
        }
        else if(event_type & EPOLLOUT)
        {
            send_if_ready(event_fd);
        }
        else if(event_type & EPOLLRDHUP)
        {
            delete_pending_response(event_fd);
        }
    }
    return connections;
}
```

This is the heart of the web server, so it is worth going into in detail. The 
basic idea of this method is that it waits for a maximum on N milliseconds for
some kind of event to appear on the watch list, and then deals with it. Each
time it is called it produces a list of new connections that have to be 
processed, sends any data that has been prepared for the connections, and 
closes any connections that have been fully processed. In addition to this it 
checks to see if the user has hit `crtl-c` or `ctrl-q`, which will trigger the 
shut down sequence. 

The method fist creates a `std::vector` to hold any new connections that have 
data that is ready to be read. Even though `handle_connections` reacts as soon 
as the first event appears, it is possible that multiple events have occurred 
before the method was called, so it is possible that there are multiple 
connections waiting in the queue. It's also possible that there are zero 
connections, but using a vector means that we don't have to worry about this.

Then it calls `epoll_wait(m_epoll_fd, m_epoll_buffer, m_max_batch_size, timeout_ms)`.
This waits at most `timeout_ms` milliseconds for an event to appear, and puts
any awaiting events into the array `m_epoll_buffer`. There are two pieces of 
information that we are interested in the event type (`event_type)` and the 
event file descriptor (`event_fd`)

The first thing we check is if this event comes form `m_sig_fd`, a file 
descriptor that was set up to watch for `crtlc/ctrl-q` signals in the method
`setup_sig_fd` in [connection.cpp](https://github.com/bwalshe/simple_http_server/blob/main/src/connection.cpp)
We are only waiting on one event type from this fd, so there is no point in
checking the type. If we get this signal, we shut down the thread-pool and
exit. 

Similarly, for `m_sock_fd` we are only waiting on one type of signal. When
we get an event for this fd, it means that the socket has a new connection
waiting to be served. The code for accepting the connection is in the 
function `accept_connection`. This will call accept to crate a new file 
descriptor for the connection, set the fd to non-blocking and then add a watch
to epoll for `EPOLLIN` signals for the fd. The kernel starts trying to receive 
data from the connection and will send an `EPOLLIN` signal as soon as it receives
*something*. Of course that *something* might not be a full request, but I will
address that later. On top of this, in most on-line resources you read on epoll, 
they watch for `EPOLLIN & EPOLLET`, which is the edge-triggered version of 
`EPOLLIN`. I am not doing that here, and this is another thing I will explain 
later on.

The next type of event that we need to deal with is `EPOLLIN`. This is the signal 
we get when there is data waiting for us on a connection. We do two things in 
this case. We delete the watch form the epoll list, and we create an 
`IncomingConnection` object to wrap the connection's file descriptor. The 
`IncomingConnection` class gives us a way of sending data back to the connection
when we are ready. This connection gets added to the vector of connections we
will return when the method is finished.

The behaviour of the `IncomingConnection` class is a bit complicated, and could 
do with being refactored. This class has a `respond()` method that takes care of
first putting a job on the thread pool queue and then putting a `std::future` 
object for the job's result onto a map of unsent responses. It also adds a watch
for `EPOLLRDHUP` signals which will tell us if the remote client broke the 
connection before we could response. But wait there is more... In addition to 
adding the response job to the thread pool queue, it tacks on an instruction to
end of that job which will create a watch for `EPOLLOUT` once that job is 
finished. The reason it does this is so that the system won't try and send the 
response until it is ready. Admittedly there are a lot of things going on at 
once in the `respond()` method, and this could have been designed better.

As we don't add listeners for `EPOLLOUT` events until the data is ready, this
means that when we receive them, the course of action is pretty simple. First
we search `m_pending_responses` to see if there is a response waiting to be 
sent if we find something we send it, and delete the object from `m_pending_responses`.
If we don't find anything then we assume that we must have already sent a
response to the connection, and that the kernel has finished sending it. It's time
to close the connection, and delete all the watches for it from our epoll list.

Finally there is one last event type we need to deal with - `EPOLLRDHUP`. This 
happens if a remote client closes the connection on us. If this happens we 
delete the corresponding response from `m_pending_resposes`, and remove all
our epoll watches for that connection fd. This is important as file descriptors
get reused for new connections, and we don't want a response intended for one 
of our old connections to be sent to a new one.
