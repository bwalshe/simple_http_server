#include <netinet/tcp.h>
#include <iostream> 
#include <cstring>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <signal.h>
#include <fcntl.h>
#include "connection.h"


int block_signals(sigset_t &mask)
{
    sigemptyset(&mask);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGINT);
    return sigprocmask(SIG_BLOCK, &mask, NULL);
}


int add_epoll_watch(int epoll_fd, int event_fd)
{
    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = event_fd;
    return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_fd, &ev); 
}


TcpConnectionQueue::TcpConnectionQueue(int port, int queue_size, int max_batch_size): 
    m_port(port), 
    m_conn_queue_size(queue_size),
    m_sock_fd(socket(AF_INET, SOCK_STREAM, 0)),
    m_alive(true),
    m_max_batch_size(max_batch_size)
{
    throw_on_err(m_sock_fd, "socket(AF_INET, SOCK_STREAM, 0)");
    throw_on_err(fcntl(m_sock_fd, F_SETFL, O_NONBLOCK), "make socket non-blocking");
    int flag = 1;
    setsockopt(m_sock_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    memset(&m_server_address, 0, sizeof(m_server_address));
    m_server_address.sin_family = AF_INET;
    m_server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    m_server_address.sin_port = htons(port);
    throw_on_err(bind(m_sock_fd, (sockaddr *)&m_server_address, 
                sizeof(m_server_address)), "bind(m_sock_fd)");
    throw_on_err(listen(m_sock_fd, m_conn_queue_size), "listen(socker_fd)");
    
    m_epoll_fd = throw_on_err(epoll_create1(0), "epoll_create");
    throw_on_err(add_epoll_watch(m_epoll_fd, m_sock_fd), "set up sock poll");

    sigset_t mask;
    throw_on_err(block_signals(mask), "block signals");
    m_sig_fd = throw_on_err(signalfd(-1, &mask, 0), "signalfd");
    throw_on_err(add_epoll_watch(m_epoll_fd, m_sig_fd), "set up sigint poll");

    m_epoll_buffer = new epoll_event[max_batch_size];
}


std::vector<TcpConnectionQueue::connection_ptr> TcpConnectionQueue::waiting_connections(int timeout_ms)
{
    std::vector<TcpConnectionQueue::connection_ptr> connections;
    
    int nfds = throw_on_err(epoll_wait(m_epoll_fd, m_epoll_buffer, m_max_batch_size, timeout_ms), "epoll_wait");
    for(auto i = 0; i < nfds; ++i)
    {
        if(m_epoll_buffer[i].data.fd == m_sig_fd) 
        {
            std::cerr << "Shutting down." << std::endl;
            m_alive = false;
            break;
        }
        if (m_epoll_buffer[i].data.fd == m_sock_fd)
        {
            sockaddr incomming_address;
            socklen_t address_size(sizeof(incomming_address));
            int fd = throw_on_err(accept(m_sock_fd, &incomming_address, &address_size), "accept(m_soc_fd)");
            // TODO: find out if there is a way of using std::make_shared with a 
            // private constructor.
            connections.push_back( std::shared_ptr<IncomingConnection>(
                    new IncomingConnection(incomming_address, address_size, fd)));
        }
    }
    return connections; 
}


std::string TcpConnectionQueue::IncomingConnection::receive()
{
    int msg_size = recv(m_request_fd, msg_buffer, MAX_PACKET_SIZE, 0);
    std::cerr << "Received a " << msg_size << " byte request." << std::endl;
    return std::string(msg_buffer, msg_size);
}


int TcpConnectionQueue::IncomingConnection::respond(Response &&response)
{
    auto response_str = static_cast<std::string>(response);
    int numBytesToSend = response_str.size();
    auto return_val =  throw_on_err(send(m_request_fd, response_str.c_str(), numBytesToSend, 0), "send");
    return return_val;
}

