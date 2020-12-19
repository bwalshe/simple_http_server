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


#define MAX_PACKET_SIZE 4096

const char* RESPONSE ="HTTP/1.1 200 OK\r\n\r\n"
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


int throw_on_err(int result)
{
    if(result == -1) throw std::runtime_error(strerror(errno));
    return result;
}

class IncomingConnection
{
    sockaddr m_address;
    socklen_t m_address_size;
    int m_request_fd;
    char msg_buffer[MAX_PACKET_SIZE];

    
    IncomingConnection(sockaddr address, socklen_t address_size, int request_fd):
        m_address(address),
        m_address_size(address_size),
        m_request_fd(request_fd) {}

public:
    ~IncomingConnection()
    {
        close(m_request_fd);
    }

    static std::shared_ptr<IncomingConnection> accept_from(int socket_fd)
    {
        sockaddr incomming_address;
        socklen_t address_size(sizeof(incomming_address));
        int fd = throw_on_err(accept(socket_fd, &incomming_address, &address_size));

        // TODO: find out if there is a way of using std::make_shared with a 
        // private constructor.
        return std::shared_ptr<IncomingConnection>(
                new IncomingConnection(incomming_address, address_size, fd)); 
    }

    std::string receive()
    {
        int msg_size = recv(m_request_fd, msg_buffer, MAX_PACKET_SIZE, 0);
        std::cerr << "Received " << msg_size << " bytes." << std::endl;
        return std::string(msg_buffer, msg_size);
    }
    
    int respond(const char* response)
    {
        int numBytesToSend = strlen(response);
        auto return_val =  throw_on_err(send(m_request_fd, response, numBytesToSend, MSG_DONTWAIT));
        return return_val;
    }
};

struct Request
{
    enum Action { GET, POST };
    const Action action;
    const std::string path;
    const std::shared_ptr<IncomingConnection> connection;

    static Request parse_request(std::shared_ptr<IncomingConnection> connection)
    {
        static std::regex const header_regex("([A-Z]+) ([^ ]+).*", std::regex_constants::extended);
        static std::unordered_map<std::string, Request::Action> const actions = { 
            {"GET", Request::GET}, 
            {"POST", Request::POST} 
        };

        std::smatch smatch;
        std::string raw_request = connection->receive();
        if(!std::regex_match(raw_request, smatch, header_regex))
        {
            throw std::runtime_error("bad request header: " + raw_request); 
        }
        auto request_action = actions.find(smatch[1]);
        if(request_action == actions.end())
        {
            throw std::runtime_error(std::string("Unrecognided HTTP action "));
        }
        return Request{request_action->second, smatch[2], connection};
    }

    int respond(const char* response)
    {
        return connection->respond(response);
    }

};

std::ostream& operator<<(std::ostream &strm, const Request &r) {
    static std::unordered_map<Request::Action, const char*> const actions = { 
            {Request::GET, "GET"}, 
            {Request::POST, "POST"} 
    };

    return strm << actions.find(r.action)->second << " " << r.path;
}

class TcpConnectionQueue
{
    const int m_port;
    const int m_conn_queue_size;
    const int m_sock_fd;

    sockaddr_in m_server_address;
   
public:
    TcpConnectionQueue(int port, int queue_size): 
        m_port(port), 
        m_conn_queue_size(queue_size),
        m_sock_fd(socket(AF_INET, SOCK_STREAM, 0))

    {
        throw_on_err(m_sock_fd);
        int flag = 1;
        setsockopt(m_sock_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
        
        memset(&m_server_address, 0, sizeof(m_server_address));
        m_server_address.sin_family = AF_INET;
        m_server_address.sin_addr.s_addr = htonl(INADDR_ANY);
        m_server_address.sin_port = htons(port);

        throw_on_err(bind(m_sock_fd, (sockaddr *)&m_server_address, 
                    sizeof(m_server_address)));
        throw_on_err(listen(m_sock_fd, m_conn_queue_size));

    }

    std::shared_ptr<IncomingConnection> next_connection()
    {
        return IncomingConnection::accept_from(m_sock_fd); 
    }
    
};

int main(int argc, char **argv)
{
    int port = 8080;
    int queue_size = 5;

    if(argc > 1) port = atoi(argv[1]);
    if(argc > 2) queue_size = atoi(argv[2]);

    TcpConnectionQueue conns(port, queue_size);

    while(true)
    {   
        auto connection =  conns.next_connection();
        try
        {
            auto r = Request::parse_request(connection);
            std::cout << r << std::endl;
            r.respond(RESPONSE);
        } catch (const std::runtime_error& e)
        {
            std::cerr << e.what() << std::endl;
            connection->respond("HTTP/1.1 500 ERROR\r\n\r\nSomething went wrong"); 
        }
    }
}
