# A basic HTTP server

This is a basic HTTP server I put together to help understand networking and 
concurrency a bit better. I was reading *C++ Concurrency in Action* by Anthony 
Williams, which contains a skeleton example of using promises and futures to 
make a non-blocking web server, but it pretty damn skeletal. 

Non-blocking web servers are a pretty interesting subject and I have used them
in the past for work projects, but I've kind of considered what is going on 
under the hood to be a kind of voodoo. I realised I was jumping the gun a bit
trying to understand non-blocking servers when I wasn't clear on how regular
servers are implemented, so that is why I started putting this project together.

This is a one-thread-per-request web server implemented using linux API calls
and the standard C++ library. A fairly simple improvement would be to get it
to use a thread pool instead of spawning new threads each request, but I think
it might take a bit of reworking to get the non-blocking behaviour shown in
William's example. I also should get it to stream requests and results instead
of passing strings around.
