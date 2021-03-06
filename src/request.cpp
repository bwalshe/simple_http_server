#include <string>
#include <regex>
#include "request.h"

Request::Action Request::get_action(const std::string &action) 
{
    if(action == "GET") return Request::GET;
    if(action == "POST") return Request::POST;
    throw std::runtime_error("Unknown HTTP action: " + action);
}

std::string Request::to_string(Action action)
{
    switch(action)
    {
        case GET: return std::string("GET");
        case POST: return std::string("POST");
        default: throw std::runtime_error("Unknown HTTP action");
    }
}


std::ostream& operator<<(std::ostream &strm, const Request &r) {
    return strm << Request::to_string(r.m_action) << " " << r.m_path << " " << r.m_query;
}


std::optional<Request> parse_request(std::shared_ptr<TcpConnectionQueue::IncomingConnection> connection)
{
    static std::regex const header_regex("([A-Z]+) ([^ ?]+)(\\?([^ #]+))?.*", 
            std::regex_constants::extended);
    std::smatch smatch;
    std::string raw_request = connection->receive();
    if(raw_request.size() == 0)
    {
        return {};
    }
    if(!std::regex_match(raw_request, smatch, header_regex))
    {
        throw std::runtime_error("Bad request header: " + raw_request); 
    }
    auto request_action = Request::get_action(smatch[1]);
    return Request(connection, request_action, smatch[2], smatch[4]);
}

