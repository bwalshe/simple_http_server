#pragma once
#include <string>
#include <memory>
#include <stdexcept>
#include "connection.h" 

struct Request
{
    enum Action { GET, POST };
    const Action action;
    const std::string path;
    const std::shared_ptr<IncomingConnection> connection;

    friend std::ostream& operator<<(std::ostream &, const Request &);
    friend Request parse_request(std::shared_ptr<IncomingConnection>);
    
    int respond(const char* response)
    {
        return connection->respond(response);
    }

private:
    static Action get_action(const std::string &action);

    static std::string to_string(Action action);

};


std::ostream& operator<<(std::ostream &strm, const Request &r);


Request parse_request(std::shared_ptr<IncomingConnection> connection);


