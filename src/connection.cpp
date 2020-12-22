#include <mutex>
#include <netinet/tcp.h>
#include <iostream> 
#include <cstring>
#include <sstream>
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


int epoll_watch(int epoll_fd, int event_fd, int event_type)
{
    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = event_type;
    ev.data.fd = event_fd;
    return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_fd, &ev); 
}


int epoll_delete(int epoll_fd, int event_fd)
{
    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data.fd = event_fd;
    return epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event_fd, &ev);
}


int setnonblocking(int fd)
{
    return fcntl(fd, F_SETFL, O_NONBLOCK);
}

TcpConnectionQueue::TcpConnectionQueue(int port, int queue_size, int max_batch_size): 
    m_port(port), 
    m_conn_queue_size(queue_size),
    m_sock_fd(socket(AF_INET, SOCK_STREAM, 0)),
    m_alive(true),
    m_max_batch_size(max_batch_size)
{
    throw_on_err(m_sock_fd, "socket(AF_INET, SOCK_STREAM, 0)");
    throw_on_err(setnonblocking(m_sock_fd), "make socket non-blocking");
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
    throw_on_err(epoll_watch(m_epoll_fd, m_sock_fd, EPOLLIN), "set up sock poll");

    sigset_t mask;
    throw_on_err(block_signals(mask), "block signals");
    m_sig_fd = throw_on_err(signalfd(-1, &mask, 0), "signalfd");
    throw_on_err(epoll_watch(m_epoll_fd, m_sig_fd, EPOLLIN), "set up sigint poll");

    m_epoll_buffer = new epoll_event[max_batch_size];
}


std::vector<TcpConnectionQueue::connection_ptr> TcpConnectionQueue::waiting_connections(int timeout_ms)
{
    std::vector<TcpConnectionQueue::connection_ptr> connections;
   
    sockaddr_in incoming_address;
    socklen_t address_size(sizeof(incoming_address));

    int nfds = throw_on_err(
            epoll_wait(m_epoll_fd, m_epoll_buffer, m_max_batch_size, timeout_ms), 
            "epoll_wait");
    for(auto i = 0; i < nfds; ++i)
    {
        int event_type = m_epoll_buffer[i].events;
        int event_fd = m_epoll_buffer[i].data.fd;

        if(event_fd == m_sig_fd) 
        {
            std::cerr << "Shutting down." << std::endl;
            m_alive = false;
            break;
        }
        if (event_fd == m_sock_fd)
        {
            int connection_fd = throw_on_err(
                    accept(m_sock_fd, (sockaddr *) &incoming_address, &address_size), 
                    "accept(m_soc_fd)");
            throw_on_err(setnonblocking(connection_fd), 
                    "make incoming connection non-blocking");
            throw_on_err(epoll_watch(m_epoll_fd, connection_fd, EPOLLIN), 
                    "Add incoming connection to epoll");
        }
        else if(event_type & EPOLLIN)
        {
            throw_on_err(epoll_delete(m_epoll_fd, event_fd),
                    "Remove incoming connection from epoll");
            connections.push_back(
                    connection_ptr(new IncomingConnection(event_fd, this)));
        }
        else if(event_type & EPOLLOUT)
        {
            throw_on_err(epoll_delete(m_epoll_fd, event_fd), 
                    "Remove outgoing connection from epoll");
            std::lock_guard<std::mutex> guard(m_response_mtx);
            Response response = m_pending_responses.find(event_fd)->second;
            size_t removed = m_pending_responses.erase(event_fd);
            if(removed != 1)
            {
                std::ostringstream out;
                out << "Attemped to serve an outgoing socket, but there were "
                    << removed << " responses available." << std::endl;
                throw std::runtime_error(out.str());
            }
            auto response_str = static_cast<std::string>(response);
            int numBytesToSend = response_str.size();
            throw_on_err(send(event_fd, response_str.c_str(), numBytesToSend, 0), "send");
            throw_on_err(close(event_fd), "Close connection");
        }
    }
    return connections; 
}


std::string TcpConnectionQueue::IncomingConnection::receive()
{
    char msg_buffer[MAX_PACKET_SIZE];
    int msg_size = recv(m_request_fd, msg_buffer, MAX_PACKET_SIZE, 0);
    std::cerr << "Received a " << msg_size << " byte request." << std::endl;
    return std::string(msg_buffer, msg_size);
}


void TcpConnectionQueue::IncomingConnection::respond(Response &&response)
{
    {
        std::lock_guard<std::mutex> guard(m_queue->m_response_mtx);
        m_queue->m_pending_responses.insert(
                std::pair<int, Response>(m_request_fd, std::move(response)));
    }

    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data.fd = m_request_fd;
    ev.events = EPOLLOUT;
    throw_on_err(epoll_watch(m_queue->m_epoll_fd, m_request_fd, EPOLLOUT),
            "Add outgoing response to epoll");
}

