#include <iostream>
#include "connection.h"
#include "request.h"
#include "response.h"
#include "request_processor.h"

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



class HelloWorldRequestHandler : public RequestHandler
{
public:
    bool matches(const Request &request)
    {
        return request.get_action() == Request::GET && request.get_path() == "/hello";
    }

    std::shared_ptr<Response> process([[maybe_unused]] const Request &request)
    {
        return std::make_shared<OK>(HELLO_RESPONSE);
    }
};


class SlowRequestHandler : public RequestHandler
{
public:
    bool matches(const Request &request)
    {
        return request.get_action() == Request::GET && request.get_path() == "/slow";
    }

    std::shared_ptr<Response> process([[maybe_unused]] const Request &request)
    {
        std::this_thread::sleep_for (std::chrono::seconds(30));
        return std::make_shared<OK>(SLOW_RESPONSE);
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


    TcpConnectionQueue conns(port, queue_size, queue_size);
    RequestProcessor processor = RequestProcessor::builder()
        .with_request_handler(new HelloWorldRequestHandler())
        ->with_request_handler(new SlowRequestHandler()) 
        ->with_not_found_response([]([[maybe_unused]] const Request &r){return NotFound(MISSING_RESPONSE);})
        ->with_error_response([]{return ServerError(ERROR);})
        ->build();

    while(conns.is_alive())
    {
       std::cerr << "Waiting for a connection." << std::endl;
       for(TcpConnectionQueue::connection_ptr connection: conns.waiting_connections(timeout))
       {
           processor.respond(connection);
       }
    }

}
