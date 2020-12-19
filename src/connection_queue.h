#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <memory>
#include "util.h"
#include "connection.h"


class TcpConnectionQueue
{
    const int m_port;
    const int m_conn_queue_size;
    const int m_sock_fd;

    sockaddr_in m_server_address;
   
public:
    TcpConnectionQueue(int port, int queue_size); 

    inline std::shared_ptr<IncomingConnection> next_connection()
    {
        return IncomingConnection::accept_from(m_sock_fd); 
    }
    
};

