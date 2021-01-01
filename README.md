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

As a machine learning engineer, I don't have to deal with this problem. I 
mostly work on offline tools used by our data scientists to build and test
their models. This means that I care a lot more about improving overall
throughput instead of latency. Real time was a subject that was interesting,
but I just didn't have the time to look into it properly. 

That was until Christmas 2020 - I went home to see my family and two days later
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
the rest of this article will be spend explaining how and why this is done.

Before we go on, I should re-iterate that I do not work on real-time stuff
professionally. I am not suggesting that this is the best way of solving the
problem, and you should not use this as a reference on how to use non-blocking 
IO. On top of this I have had no involvement in the development of the real-time 
component of Featurespace's product, I have never even looked at that part of 
our codebase and this repo is probably not representative of how our product
works. This code has not been through a review, I rarely use C++, and it is the
first time I have written a threaded C++ program - so *cavet emptor*.

