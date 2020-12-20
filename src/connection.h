#pragma once

#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <memory>
#include <vector>
#include "util.h"
#include "response.h"


#define MAX_PACKET_SIZE 4096


class TcpConnectionQueue
{
public:
    class IncomingConnection;

    using connection_ptr = std::shared_ptr<IncomingConnection>;
    
    TcpConnectionQueue(int port, int queue_size); 
 
    inline bool is_alive() { return m_alive;};
    
    std::vector<connection_ptr> waiting_connections(int timeout_ms);

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

        std::string receive();
        
        int respond(Response &&response);


        friend std::vector<connection_ptr> TcpConnectionQueue::waiting_connections(int timeout_ms);

    };

private:
    const int m_port;
    const int m_conn_queue_size;
    const int m_sock_fd;
    int m_epoll_fd;
    int m_sig_fd;
    sockaddr_in m_server_address;
    mutable bool m_alive;
    static constexpr int MAX_EVENTS = 10;
     
};


