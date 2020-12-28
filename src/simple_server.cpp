#include <iostream>
#include <future>
#include <oneapi/tbb/task_arena.h>
#include "connection.h"
#include "request.h"
#include "response.h"


const char* HELLO_RESPONSE =
R"(
<html>
    <head>
        <title>This is a quick response</title>
    </head>
    <body>
        <p>Hello world!</p>
    </body>
</html>
)";

const char* SLOW_RESPONSE =
R"(
<html>
    <head>
        <title>This is a slow response</title>
    </head>
    <body>
        <p>Sorry that took so long.</p>
    </body>
</html>
)";

const char* MISSING_RESPONSE =
R"(
<html>
    <head>
        <title>404: Not Found</title>
    </head>
    <body>
        <p>404: Not found.</p>
    </body>
</html>
)";

const char* ERROR =  "Something went wrong";


class RequestHandler
{
public:
    virtual bool matches(const Request &request) = 0;
    virtual void process(Request &&request) = 0;
    virtual ~RequestHandler(){}
};


class HelloWorldRequestHandler : public RequestHandler
{
public:
    bool matches(const Request &request)
    {
        return request.get_action() == Request::GET && request.get_path() == "/hello";
    }

    void process(Request &&request)
    {
        request.respond(OK(HELLO_RESPONSE));
    }
};


class SlowRequestHandler : public RequestHandler
{
public:
    bool matches(const Request &request)
    {
        return request.get_action() == Request::GET && request.get_path() == "/slow";
    }

    void process(Request &&request)
    {
        std::this_thread::sleep_for (std::chrono::seconds(30));
        request.respond(OK(SLOW_RESPONSE));
    }
};


class NotFoundRequestHandler : public RequestHandler
{
public:
    bool matches([[maybe_unused]] const Request &request)
    {
        return true;
    }

    void process(Request &&request)
    {
        request.respond(NotFound(MISSING_RESPONSE));
    }
};

class ConnectionProcessor
{
    oneapi::tbb::task_arena m_arena;
    std::vector<RequestHandler*> m_handlers;

public:
    ConnectionProcessor(int threads, std::vector<RequestHandler*> &&handlers):
        m_arena(threads),
        m_handlers(std::move(handlers)) {
        }

    void process(TcpConnectionQueue::connection_ptr connection)
    {
        try
        {
            auto r = parse_request(connection);
            if(r.has_value())
            {
                for(auto handler: m_handlers)
                {
                    if(handler->matches(r.value()))
                    {
                        std::shared_ptr<Request> request(std::make_shared<Request>(std::move(r.value())));
                        m_arena.enqueue([=]{handler->process(std::move(*request));});
                        break;
                    }
                }
            }
        }
        catch (const std::runtime_error& e)
        {
            std::cerr << e.what() << std::endl;
            connection->respond(ServerError(ERROR));
        }
    }
};

int main(int argc, char **argv)
{
    int port = 8080;
    int timeout = 30000;
    int queue_size = 10;

    if(argc > 1) port = atoi(argv[1]);
    if(argc > 2) timeout = atoi(argv[2]);
    if(argc > 3) queue_size = atoi(argv[3]);


    NotFoundRequestHandler not_Found_handler;
    HelloWorldRequestHandler hello_handler;
    SlowRequestHandler slow_handler;
    TcpConnectionQueue conns(port, queue_size, queue_size);
    ConnectionProcessor processor(10,
        std::vector<RequestHandler*> {
            &hello_handler,
            &slow_handler,
            &not_Found_handler
        });

    while(conns.is_alive())
    {
       std::cerr << "Waiting for a connection." << std::endl;
       for(TcpConnectionQueue::connection_ptr connection: conns.waiting_connections(timeout))
       {
           processor.process(connection);
       }
    }

}
