# A Simple(ish) HTTP Server with Asynchronous IO 

I work in a company that provides a real-time, machine learning, fraud
detection product. The phrase *real-time* gets thrown around a lot these
days, and I have heard it used to describe systems that produce results 
within a second or two. For us though, it has a very specific definition -
*real-time* means that clients will connect to our system, send a request, 
and expect a response back on the same connection within X milliseconds. If 
it takes any longer, then we will probably be in breach of some SLA and could
face some kind of financial penalty.

<img align="right" src="docs/covid_homer.jpg" alt="Christmas 2020"/>

As a machine learning engineer, I don't have to deal with this problem directly.
I mostly work on offline tools used by our data scientists to build and test
their models. This means that I care a lot more about improving overall
throughput instead of latency. Real time was a subject that was interesting,
but I just didn't have the time to look into it properly. 

That was until Christmas 2020. I went home to see my family and two days later
I got a text message from the Irish Government saying that I had to remain in 
my room for the next two weeks. I couldn't find anything good on Netflix, so
I decided to crack open my dusty copy of 
[C++ Concurrency in Action](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition)
and try to get to grip with this real time stuff.

The result was the HTTP server contained in the repo. On their own, HTTP 
servers are not strictly Real Time Systems, as there is no hard limit on their
response time. In practice though, it's usually pretty important that they 
respond to many concurrent connections with as low a latency as possible, and
they can be used as a *component* in a real time system. The tactic I have
gone with to reduce the latency is to use non-blocking IO, and the bulk of 
the this article will focus on explaining how and why this is done.

Before we go on, I should re-iterate that I do not work on real-time stuff
professionally. I am not suggesting that this is the best way of solving the
problem, and you should not use this as a reference on how to use non-blocking 
IO. On top of this I have had no involvement in the development of the real-time 
component of Featurespace's products, I have never even looked at that part of 
our codebase and this repo is probably not representative of how any of our 
product work. This code has not been through a review, I rarely use C++, and it
is the first time I have written a threaded C++ program - so *caveat emptor*.

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
connection is coming from but we don't even use it. We just read and write to 
the connection like it was a regular file, using the file descriptor. First we
read a bunch of bytes, then we process them and write the result into a buffer
using the `process_request()` before writing that buffer back to the connection 
and closing it. On the users' side they are going to see their loading icon spin
from the moment the connection is created, right up until the moment `close()` 
returns, at which point the page we sent them will start rendering.

It's not very realistic to expect the requests and responses to always fit in a
fixed size buffer, so in reality the web server would probably have to do 
several reads and writes to complete a request. I'm going to gloss over that 
for now and assume that all requests can be serverd by reading once, writing 
once and then closing the connection. The above implementation has a much 
bigger problem - it's not capable of serving more than one connection at a 
time.


## Serving more than one connection at a time
Serving one request at a time is not very practical. If we were serving up web
pages then users are not going to see any results until everyone ahead of them
gets served, it's going to look like the site is broken and they will probably 
hit refresh, generating a new request which goes to the back of the queue. If
the server is supporting a real time system, then the efect could be even more
disasterous. One slow request could cause all the requests that come after it
to be delayed - instead of missing our SLA on one request, we end up missing it
on **every** request. Even if we can engineer `process_request()` to complete in 
a split second, we can never garauntee that `read()` and `write()` will be 
quick. They are transferring data over a network so they are outside of our 
control. The simple solution to this is pretty straightforward: for each new
connection spin up a thread to do the read/pocess/write steps. If one of the
requests ends up being slow, it's fine, the others will be unaffected.

![Using threads to serve requests](docs/threaded_http.svg)

Spawning a thread in `C` is a kind of awkward, and can involve scary `(void *)`
casts, but in `C++` it's pretty clean. You can use 
[`std::thread`](https://en.cppreference.com/w/cpp/thread/thread) to run a 
function that doesn't return anything, or
[`std::async`](https://en.cppreference.com/w/cpp/thread/async) to run a 
function and get the result packaged in an 
[`std::future`](https://en.cppreference.com/w/cpp/thread/future). I'm not going
to code up the simple multi threaded server, but it would look pretty similar
to the above, only with the read/write/close part wrapped up in a function
`void process_connection(int conn_fd)`, and the main loop would become

```c++
for(;;)
{
    int conn_fd = accept(sock_fd, (sockaddr *)&conn_addr, &conn_addr_len);
    std::thread t(&proces_connection, conn_fd);
    t.detatch();
}
```

This will work pretty well in a lot of cases, but it has a few issues.
First of all spawning a thread is not free and is going to introduce some 
latency and secondly there is no upper limit on how many threads this thing
will try to spawn at once. I'm not super worried about the latency, as Linux
thread creation is fairly snappy, but the unbounded thread creation is scary.
If 10,000 clients all start sending requests at the same time, we could have
100k+ threads all vying for the same resources and that is probably not going
to go well.

![We call it, "Three Stooges Syndrome"](docs/3_stooges_syndrome.png)


## Limiting the number of concurrent threads

Instead of spawning a new thread for each connection as needed, we could spawn a
fixed size set of worker threads which live for the entire lifetime of our 
server. Each time a new connection comes in, we could pass it off to one of 
these workers and let it do its thing. This would eliminate the pause while we
wait for a thread to be created, and it would prevent us creating too many 
threads at once and jamming up the system.

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
 2. Put the connection on a 

**On one of the "worker" threads**
 1. Pull an awaiting connection off the queue
 2. Read data from the connection
 3. Generate a response
 4. Send the data

![](docs/thread_queue.svg)


This is a pretty standard pattern in concurrent programming known as a **Thread
Pool**. Thread pools are more general than the what is described above -
instead of putting connections on the queue, we put generic *jobs* on there, 
and the thread pool executes whatever is in the job instead of a specific set
of instructions to read a connection and generate a response.

If we were using a JVM language then we could just use the built in method 
[Executors.fixedThreadPool(int n)](https://docs.oracle.com/en/java/javase/11/docs/api/java.base/java/util/concurrent/Executors.html#newFixedThreadPool(int)) 
which will give us an 
[ExecutorService](https://docs.oracle.com/en/java/javase/11/docs/api/java.base/java/util/concurrent/ExecutorService.html)
object that we can submit jobs to. Submitting a job to a Java `ExecutorService`
is a bit like using `std::async`, except it won't always spin up a new thread
for you. In the case of a thread pool backed `ExecutorService` it has a finite
number of threads that are already running, which it will submit the job to.

Unfortunately, there is no thread pool implementation built in to the standard
C++ libraries. Originally I had hoped to avoid implementing my own, and I wanted
to use the [Intel Thread Building Blocks](https://github.com/oneapi-src/oneTBB),
but it seems that this library was created before C++14 and they are still in 
the process of adapting to the new standard, meaning that it doesn't behave
well with [std::future](https://en.cppreference.com/w/cpp/thread/future) and 
[std::packaged_task](https://en.cppreference.com/w/cpp/thread/packaged_task). 
Futures and packaged tasks are going to become important later on when we start
using non-blocking IO, so I decided to abandon TBB and just make my own 
threadpool. Luckily I had seen something really similar in the last chapter of 
[The Rust Programming Language](https://doc.rust-lang.org/book/ch20-02-multithreaded.html)
by Klabnik and Nichols, so I knew it would be straightforward enough. 
