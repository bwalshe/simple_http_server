#include <functional>
#include <utility>
#include <future>
#include <netinet/tcp.h>
#include <iostream>
#include <cstring>
#include <sstream>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <signal.h>
#include <fcntl.h>
#include <thread>
#include "connection.h"


int block_signals(sigset_t &mask)
{
    sigemptyset(&mask);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGINT);
    return sigprocmask(SIG_BLOCK, &mask, NULL);
}


int epoll_watch(int epoll_fd, int event_fd, int event_type, bool mod=false)
{
    int watch_type = mod ? EPOLL_CTL_MOD : EPOLL_CTL_ADD; 
    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = event_type;
    ev.data.fd = event_fd;
    return epoll_ctl(epoll_fd, watch_type, event_fd, &ev);
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
    m_max_batch_size(max_batch_size),
    m_response_threads(10)
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
            ResponseTable::accessor accessor;

            bool found = m_pending_responses.find(accessor, event_fd);
            if(!found)
            {
                std::ostringstream out;
                out << "Attemped to serve an outgoing socket, but there were "
                    << "no responses available." << std::endl;
                throw std::runtime_error(out.str());
            }
            if(accessor->second.valid())
            {
                auto response_str = static_cast<std::string>(*(accessor->second.get()));
                m_pending_responses.erase(accessor);
                accessor.release();
                int numBytesToSend = response_str.size();
                throw_on_err(send(event_fd, response_str.c_str(), numBytesToSend, 0), "send");
                throw_on_err(close(event_fd), "Close connection");
            }
        }
        else if(event_type & EPOLLRDHUP)
        {
            std::cerr << "got EPOLLHUP on " << event_fd << std::endl;
            ResponseTable::accessor accessor;
            if(m_pending_responses.find(accessor, event_fd))
            {
                std::cerr << "Deleting unset response as client connection has been closed." << std::endl;
                m_pending_responses.erase(accessor);
            }
            throw_on_err(epoll_delete(m_epoll_fd, event_fd), "removing closed connection from epoll");

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


void TcpConnectionQueue::IncomingConnection::respond(std::function<std::shared_ptr<Response>(void)> &&response)
{
    using response_task = std::packaged_task<std::shared_ptr<Response>(void)>; 
    response_task task([=](){
            auto r = response();
            throw_on_err(epoll_watch(m_queue->m_epoll_fd, m_request_fd, EPOLLOUT, true),
                "Add outgoing response to epoll");
            return r;
    });
    
    {
        ResponseTable::accessor accessor;
        if(m_queue->m_pending_responses.insert(accessor, m_request_fd)) {
            accessor->second = task.get_future();
        } 
        else
        {
            throw std::runtime_error("Could not add response to outgoing queue");
        }
    }
  
    throw_on_err(epoll_watch(m_queue->m_epoll_fd, m_request_fd, EPOLLRDHUP),
            "Add outgoing response to epoll");
    m_queue->m_response_threads.execute(std::move(task));

}

