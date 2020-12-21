#pragma once
#include <string>
#include <memory>
#include <stdexcept>
#include <optional>
#include "connection.h" 
#include "response.h"

//
// A parsed HTTP request includiong the HTTP action, the path, and there should be a bunch of other stuff
//
class Request
{
public:
    enum Action { GET, POST };

private:
    const Action m_action;
    const std::string m_path;
    const std::string m_query;
    const TcpConnectionQueue::connection_ptr m_connection;
    
    Request(TcpConnectionQueue::connection_ptr connection, Action action, std::string path, std::string query) : 
        m_action(action), m_path(path), m_query(query), m_connection(connection) {}

public:
    
    Action get_action() const 
    {
        return m_action;
    }

    const std::string& get_path() const
    {
        return m_path;
    }

    const std::string& get_query() const 
    {
        return m_query;
    }   
    
    //
    // Send a response to this request. The response will be consumed and cannot be used again.
    //
    int respond(Response &&response)
    {
        return m_connection->respond(std::move(response));
    }
    
    friend std::ostream& operator<<(std::ostream &, const Request &);
    friend std::optional<Request> parse_request(std::shared_ptr<TcpConnectionQueue::IncomingConnection>);

private:
    
    static Action get_action(const std::string &action);

    static std::string to_string(Action action);

};


std::ostream& operator<<(std::ostream &strm, const Request &r);


std::optional<Request> parse_request(std::shared_ptr<TcpConnectionQueue::IncomingConnection> connection);


