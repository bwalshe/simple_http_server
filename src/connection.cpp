#include <netinet/tcp.h>
#include <iostream> 
#include <cstring>
#include "connection.h"

std::shared_ptr<IncomingConnection> IncomingConnection::accept_from(int socket_fd)
{
    sockaddr incomming_address;
    socklen_t address_size(sizeof(incomming_address));
    int fd = throw_on_err(accept(socket_fd, &incomming_address, &address_size));

    // TODO: find out if there is a way of using std::make_shared with a 
    // private constructor.
    return std::shared_ptr<IncomingConnection>(
            new IncomingConnection(incomming_address, address_size, fd)); 
}

std::string IncomingConnection::receive()
{
    int msg_size = recv(m_request_fd, msg_buffer, MAX_PACKET_SIZE, 0);
    std::cerr << "Received " << msg_size << " bytes." << std::endl;
    return std::string(msg_buffer, msg_size);
}

int IncomingConnection::respond(const char* response)
{
    int numBytesToSend = strlen(response);
    auto return_val =  throw_on_err(send(m_request_fd, response, numBytesToSend, MSG_DONTWAIT));
    return return_val;
}

