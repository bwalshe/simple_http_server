#pragma once

#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <memory>
#include <vector>
#include <sys/epoll.h>
#include "util.h"
#include "response.h"


#define MAX_PACKET_SIZE 4096


//
// This class sets up a tcp socket in non-blocking mode and then monitors it
// for incomming connections using epoll. Once the queue is set up, incoming
// connections can be pulled off the queue in batches using 
// TcpConnectionQueue::waiting_connections(int). 
//
// In addition, this class will intercept SIGINT and SIGQUIT. If either
// of these signals are recieived the queue will shut down and stop accepting
// new conneections.
class TcpConnectionQueue
{
public:
    class IncomingConnection;

    using connection_ptr = std::shared_ptr<IncomingConnection>;
    
    //
    // Create a new non blocking tcp socket on the specified port
    // Args:
    //  :port: the port number to use
    //  :queue_size: the maximum number of unanswered connections on this port
    //  :max_batch_size: the maximum number of connections that will be pulled
    //  from the queue in one go
    //
    TcpConnectionQueue(int port, int queue_size, int max_batch_size); 

    ~TcpConnectionQueue() {
       delete[] m_epoll_buffer;
    }

    //
    // Is this tcp sockety still being serverd. This value will be set to false
    // if the running program receives the SIGINT or SIGQUIT signals.
    //
    inline bool is_alive() { return m_alive;};
    
    std::vector<connection_ptr> waiting_connections(int timeout_ms);

    //
    // A class to keep track of the incoming connections and enable IO 
    // operations with them.
    //
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

        //
        // Try to reead a string from the connection.
        //
        std::string receive();
       
        //
        // Send a response back to the connection.
        //
        // Note the response object passed to this method is consumed and cannot be used again.
        //
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
    int m_max_batch_size;
    epoll_event *m_epoll_buffer;
     
};


