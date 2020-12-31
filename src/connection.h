#pragma once

#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <memory>
#include <vector>
#include <future>
#include <sys/epoll.h>
#include <oneapi/tbb/concurrent_hash_map.h>
#include "util.h"
#include "response.h"
#include "thread_pool.h"
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
    TcpConnectionQueue(int port, int os_queue_size, int max_batch_size);

    ~TcpConnectionQueue() {
        m_thread_pool.shutdown();
        delete[] m_epoll_buffer;
    }

    //
    // Is this tcp sockety still being serverd. This value will be set to false
    // if the running program receives the SIGINT or SIGQUIT signals.
    //
    inline bool is_alive() { return m_alive; };


    //
    // This is the main loop for dealing with incoming and outgoing connections.
    // When it runs it will send any pending outgoing data, and gather a vector
    // of incoming connectons that are ready to send data to the server.
    // Args:
    //   :timeout_ms: The maximum amount of time to wait for an incoming
    //                connection
    // Returns:
    //    A vector of connections which are ready to be processed.
    std::vector<connection_ptr> handle_connections(int timeout_ms);

    //
    // A class to keep track of the incoming connections and enable IO
    // operations with them.
    //
    class IncomingConnection
    {
        int m_request_fd;
        TcpConnectionQueue *m_queue;

        IncomingConnection(int request_fd, TcpConnectionQueue *queue):
            m_request_fd(request_fd), m_queue(queue) {}

    public:

        //
        // Try to read a string from the connection.
        //
        std::string receive();

        //
        // Send a response back to the connection.
        //
        // Note the response object passed to this method is cannot be used again.
        //
        void respond(std::function<std::shared_ptr<Response>(void)> &&);

        friend std::vector<connection_ptr> TcpConnectionQueue::handle_connections(int timeout_ms);
    };

    friend void IncomingConnection::respond(std::function<std::shared_ptr<Response>(void)> &&);

private:

    using ResponseTable = oneapi::tbb::concurrent_hash_map<int, std::future<std::shared_ptr<Response>>>;

    void shutdown();
    void send_pending_response(int connection_fd);
    void delete_pending_response(int connection_fd);

    const int m_sock_fd;
    const int m_sig_fd;
    const int m_epoll_fd;
    mutable bool m_alive;
    const int m_max_batch_size;
    epoll_event *m_epoll_buffer;
    ResponseTable m_pending_responses;
    ThreadPool<10, std::shared_ptr<Response>> m_thread_pool;
};

