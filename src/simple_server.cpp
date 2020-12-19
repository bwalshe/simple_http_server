#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <stdexcept>
#include <iostream>
#include <errno.h>
#include <cstring>
#include <netinet/tcp.h>
#include <memory>
#include <optional>
#include <regex>
#include <unordered_map>
#include <future>
#include "connection.h"
#include "request.h"
#include "connection_queue.h"


constexpr int MAX_PACKET_SIZEi = 4096;

const char* RESPONSE = "HTTP/1.1 200 OK\r\n\r\n"
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

const char* ERROR =  "HTTP/1.1 500 ERROR\r\n\r\nSomething went wrong";

void process_request(std::shared_ptr<IncomingConnection> connection)
{   
    try
    {
        auto r = parse_request(connection);
        std::cout << r << std::endl;
        r.respond(RESPONSE);
    } catch (const std::runtime_error& e)
    {
        std::cerr << e.what() << std::endl;
        connection->respond(ERROR); 
    }

}


int main(int argc, char **argv)
{
    int port = 8080;
    int queue_size = 5;

    if(argc > 1) port = atoi(argv[1]);
    if(argc > 2) queue_size = atoi(argv[2]);

    TcpConnectionQueue conns(port, queue_size);

    while(true)
    {   
        process_request(conns.next_connection());
    }
}
