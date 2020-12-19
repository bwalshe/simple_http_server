#include <memory.h>
#include "connection_queue.h"


TcpConnectionQueue::TcpConnectionQueue(int port, int queue_size): 
    m_port(port), 
    m_conn_queue_size(queue_size),
    m_sock_fd(socket(AF_INET, SOCK_STREAM, 0))

{
    throw_on_err(m_sock_fd);
    int flag = 1;
    setsockopt(m_sock_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    
    memset(&m_server_address, 0, sizeof(m_server_address));
    m_server_address.sin_family = AF_INET;
    m_server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    m_server_address.sin_port = htons(port);

    throw_on_err(bind(m_sock_fd, (sockaddr *)&m_server_address, 
                sizeof(m_server_address)));
    throw_on_err(listen(m_sock_fd, m_conn_queue_size));

}

