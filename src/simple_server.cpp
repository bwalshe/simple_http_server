#include <iostream>
#include <future>
#include "connection.h"
#include "request.h"
#include "response.h"

constexpr int MAX_PACKET_SIZEi = 4096;

const char* RESPONSE = 
R"(
<html>
    <head>
        <title>This is a response</title>
    </head>
    <body>
        <p>Hello world!</p>
    </body>
</html>
)";

const char* ERROR =  "Something went wrong";

void process_request(TcpConnectionQueue::connection_ptr connection)
{   
    try
    {
        auto r = parse_request(connection);
        std::cout << r << std::endl;
        r.respond(OK(RESPONSE));
    }
    catch (const std::runtime_error& e)
    {
        std::cerr << e.what() << std::endl;
        connection->respond(ServerError(ERROR)); 
    }
}


int main(int argc, char **argv)
{
    int port = 8080;
    int timeout = 5000;
    int queue_size = 5;

    if(argc > 1) port = atoi(argv[1]);
    if(argc > 2) timeout = atoi(argv[2]);
    if(argc > 3) queue_size = atoi(argv[3]);

    TcpConnectionQueue conns(port, queue_size);

    while(conns.is_alive())
    {  
       std::cerr << "Watitng for a connection." << std::endl;
       for(TcpConnectionQueue::connection_ptr connection: conns.waiting_connections(timeout))
       {    
           static_cast<void>(std::async(&process_request, connection));
       }
    }
}
