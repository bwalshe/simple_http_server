# A Simple(ish) HTTP Server with Asynchronous IO 

I work in a company that provides a real-time, machine learning, fraud
detection product. The phrase *real-time* gets thrown around a lot these
days, and I have heard it used to describe systems that produce results 
within a second or two. For us though, it has quite a specific definition -
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

If you wanted to implement this all you'd need is something like the following:

```c
char in_buffer[BUFFER_SIZE]; // A buffer for the incoming data
char out_buffer[BUFFER_SIZE]; // and for the outgoing data
int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
/* 
Then some cryptic incantations to bind the socket to a port and put it in 
listen mode.
*/

struct sockaddr_in conn_addr; // This will hold the incoming address, note that
                              // I don't actually use it for anything.
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
and closing it. On the users side they are going to see their loading icon spin
from the moment the connection is created, right up until the moment `close()` 
returns, at which point the page we sent them will start rendering.
