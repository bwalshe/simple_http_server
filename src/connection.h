#pragma once

#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <memory>
#include "util.h"

#define MAX_PACKET_SIZE 4096


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

    static std::shared_ptr<IncomingConnection> accept_from(int socket_fd);

    std::string receive();
    
    int respond(const char* response);
};

