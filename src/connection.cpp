#include <stdexcept>
#include <utility>
#include <netinet/tcp.h>
#include <cstring>
#include <sstream>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <signal.h>
#include <fcntl.h>
#include "connection.h"


//
// Tell eopll to start watching for events on the specified file descriptor
// args:
//  :epoll_fd: the file descriptor for the poll
//  :event_fd: the file descriptor we want events for
//  :event_type: the event type(s) we are looking for
//  :modify: Are we adding a new watch for a new file descriptor, or making a change
//           to the event type of events being watched for on an exising wathc?
//           Modifying the event type on a watch that doesn't exist, or vice versa
//           will result in a runtime execption.
//
int epoll_watch(int epoll_fd, int event_fd, int event_type, bool modify=false)
{
    int watch_type = modify ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = event_type;
    ev.data.fd = event_fd;
    return epoll_ctl(epoll_fd, watch_type, event_fd, &ev);
}


//
// Stop watching for events on the specified file descriptor
//
int epoll_delete(int epoll_fd, int event_fd)
{
    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data.fd = event_fd;
    return epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event_fd, &ev);
}


//
// Set the file descriptor to non-blocking mode.
// This will mean that if we read/write to the fd when it is not ready
// an error will occur.
//
int setnonblocking(int fd)
{
    return fcntl(fd, F_SETFL, O_NONBLOCK);
}


//
// Open up a non-blocking socket listening on the specified port.
// args:
//  :port: the port to listen on
//  :connection_queue_size: The maximim number of unanswered connections that
//                          OS will hold for us.
//
int setup_socket(unsigned int port, int connection_queue_size)
{
    int sock_fd = throw_on_err(socket(AF_INET, SOCK_STREAM, 0), "create socket");
    throw_on_err(setnonblocking(sock_fd), "make socket non-blocking");
    int flag = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(port);
    throw_on_err(bind(sock_fd, (sockaddr *)&server_address,
                sizeof(server_address)), "bind socket to address");
    throw_on_err(listen(sock_fd, connection_queue_size), "set socket to listen");
    return sock_fd;
}

//
// Block the default handlers for SIGINT and SIGQUIT and give us a file
// descriptor so that we can manually listen for these events on epoll
//
int setup_sig_fd()
{
    sigset_t mask = block_signals();
    return throw_on_err(signalfd(-1, &mask, 0), "signalfd");
}


//
// Set up an epoll file handle and tell it to listen for events from our
// open socket and the signals that we blocked.
//
int setup_epoll(int sock_fd, int sig_fd)
{
    int epoll_fd = throw_on_err(epoll_create1(0), "epoll_create");
    throw_on_err(epoll_watch(epoll_fd, sock_fd, EPOLLIN), "set up sock poll");
    throw_on_err(epoll_watch(epoll_fd, sig_fd, EPOLLIN), "set up sigint poll");
    return epoll_fd;
}


TcpConnectionQueue::TcpConnectionQueue(int port, int os_queue_size, int max_batch_size):
    m_sock_fd(setup_socket(port, os_queue_size)),
    m_sig_fd(setup_sig_fd()),
    m_epoll_fd(setup_epoll(m_sock_fd, m_sig_fd)),
    m_alive(true),
    m_max_batch_size(max_batch_size)
{
    m_epoll_buffer = new epoll_event[max_batch_size];
}


//
// Once this has been called, `is_alive()` will start to return false,
// and the response threadpool will be shutdown.
//
void TcpConnectionQueue::shutdown()
{
    std::cerr << "Shutting down." << std::endl;
    m_alive = false;
    m_thread_pool.shutdown();
}

//
// This is called when we recieve an epoll event telling
// us that sock_fd has an incoming connection. Adds
// a watch to epoll to tell us when that conneciton
// has data available for reading.
//
void accept_connection(int sock_fd, int epoll_fd)
{
    sockaddr_in incoming_address;
    socklen_t address_size(sizeof(incoming_address));
    int connection_fd = throw_on_err(
            accept(sock_fd, (sockaddr *) &incoming_address, &address_size),
            "accept(m_soc_fd)");
    throw_on_err(setnonblocking(connection_fd),
                 "make incoming connection non-blocking");
    throw_on_err(epoll_watch(epoll_fd, connection_fd, EPOLLIN),
                 "Add incoming connection to epoll");
}


//
// This is called when we revieve an event from epoll telling us that a
// connection is ready to recieve data.
//
// The response here is a bit more simplistic than in a real web server, as we
// assume that all the data can be sent in one go, which means that we have a
// lot less book keeping to do.
//
// The method is stateful and the behaviour is going to depend on the pending
// responses. If there is no response future for the connection, then we assume
// that the response has already been sent. We close the connection and
// remove it from the epoll watch-list. If we do find a resoponse future, then
// we remove it from the set of pending responses, extract its data and send
// them to the awaiting connection.
//
// If the future isn't ready, then the thread is going to block while it waits
// for the data. That would be really bad, as this one thread is dealing with
// all the incoming connections. This won't be a problem thouhgh because
// elsewhere in the code, we have been careful to only add the watch for this
// connection *after* the response data are ready to send.
//
void TcpConnectionQueue::send_if_ready(int connection_fd)
{
    ResponseTable::accessor accessor;
    bool found = m_pending_responses.find(accessor, connection_fd);
    if(!found)
    {
        throw_on_err(epoll_delete(m_epoll_fd, connection_fd),
                    "Remove outgoing connection from epoll");
        throw_on_err(close(connection_fd), "Close connection");
    }
    else if(accessor->second.valid())
    {
        auto response_str = static_cast<std::string>(*(accessor->second.get()));
        m_pending_responses.erase(accessor);
        accessor.release();
        int numBytesToSend = response_str.size();
        throw_on_err(send(connection_fd, response_str.c_str(), numBytesToSend, 0), "send");
    }
    else
    {
        throw std::runtime_error("Attempted to send a response that  has already been served");
    }
}

//
// If the connection is closed while the data was being prepared then we need
// to delete it from our set of pending responses. Apart from using up memory,
// having stale responses could cause really strange behaviour when a new
// connection comes in using the same file descriptor of an old connection.
//
void TcpConnectionQueue::delete_pending_response(int connection_fd)
{
    ResponseTable::accessor accessor;
    if(m_pending_responses.find(accessor, connection_fd))
    {
        m_pending_responses.erase(accessor);
    }
    throw_on_err(epoll_delete(m_epoll_fd, connection_fd), "removing closed connection from epoll");
}


std::vector<TcpConnectionQueue::connection_ptr> TcpConnectionQueue::handle_connections(int timeout_ms)
{
    std::vector<TcpConnectionQueue::connection_ptr> connections;

    int nfds = throw_on_err(
            epoll_wait(m_epoll_fd, m_epoll_buffer, m_max_batch_size, timeout_ms),
            "epoll_wait");
    for(auto i = 0; i < nfds; ++i)
    {
        int event_type = m_epoll_buffer[i].events;
        int event_fd = m_epoll_buffer[i].data.fd;

        if(event_fd == m_sig_fd)
        {
            shutdown();
        }
        else if (event_fd == m_sock_fd)
        {
            accept_connection(m_sock_fd, m_epoll_fd);
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
            send_if_ready(event_fd);
        }
        else if(event_type & EPOLLRDHUP)
        {
            delete_pending_response(event_fd);
        }
    }
    return connections;
}


std::string TcpConnectionQueue::IncomingConnection::receive()
{
    char msg_buffer[MAX_PACKET_SIZE];
    int msg_size = recv(m_request_fd, msg_buffer, MAX_PACKET_SIZE, 0);
    return std::string(msg_buffer, msg_size);
}


void TcpConnectionQueue::IncomingConnection::respond(std::function<std::shared_ptr<Response>(void)> &&response)
{
    throw_on_err(epoll_watch(m_queue->m_epoll_fd, m_request_fd, EPOLLRDHUP),
            "Add outgoing response to epoll");
    {
        ResponseTable::accessor accessor;
        if(m_queue->m_pending_responses.insert(accessor, m_request_fd)) {
            accessor->second = m_queue->m_thread_pool.submit([=](){
                auto r = response();
                throw_on_err(epoll_watch(m_queue->m_epoll_fd, m_request_fd, EPOLLOUT | EPOLLRDHUP, true),
                    "Add outgoing response to epoll");
                return r;
            });
        }
        else
        {
            throw std::runtime_error("Could not add response to outgoing queue");
        }
    }
}

