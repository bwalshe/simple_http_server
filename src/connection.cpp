#include <netinet/tcp.h>
#include <iostream> 
#include <cstring>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <signal.h>
#include <fcntl.h>
#include "connection.h"


TcpConnectionQueue::TcpConnectionQueue(int port, int queue_size): 
    m_port(port), 
    m_conn_queue_size(queue_size),
    m_sock_fd(socket(AF_INET, SOCK_STREAM, 0)),
    m_alive(true)
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

    epoll_event soc_ev;
    memset(&soc_ev, 0, sizeof(soc_ev));
    soc_ev.events = EPOLLIN;
    soc_ev.data.fd = m_sock_fd;
    throw_on_err(epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_sock_fd, &soc_ev), 
            "epoll_ctl: add sock poll");

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGINT);
    epoll_event sig_ev;
    memset(&sig_ev, 0, sizeof(sig_ev));
    throw_on_err(sigprocmask(SIG_BLOCK, &mask, NULL), "block signals");
    m_sig_fd = throw_on_err(signalfd(-1, &mask, 0), "signalfd");
    sig_ev.data.fd = m_sig_fd;
    sig_ev.events = EPOLLIN;

    throw_on_err(epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_sig_fd, &sig_ev), 
            "epoll_ctl: add sigint poll");


}

std::vector<TcpConnectionQueue::connection_ptr> TcpConnectionQueue::waiting_connections(int timeout_ms)
{
    static epoll_event events[MAX_EVENTS];

    std::vector<TcpConnectionQueue::connection_ptr> connections;
    
    int nfds = throw_on_err(epoll_wait(m_epoll_fd, events, MAX_EVENTS, timeout_ms), "epoll_wait");
    for(auto i = 0; i < nfds; ++i)
    {
        if(events[i].data.fd == m_sig_fd) {
            std::cerr << "Shutting down." << std::endl;
            m_alive = false;
            break;
        }
        if (events[i].data.fd == m_sock_fd)
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

