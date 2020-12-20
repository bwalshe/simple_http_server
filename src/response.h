#pragma once
#include <sstream>
#include <string>

class Response
{
    std::string m_header;
    std::string m_body;

    
public:
    Response(const std::string &header, const std::string &body):
        m_header(header), m_body(body)
    {}

    /*Response(const std::string &header, std::ostringstream &&body)
    {
        m_data << header << "\r\n\r\n" << body;
    }*/

    virtual ~Response(){}

    operator std::string()
    {
        return m_header + "\r\n\r\n" + m_body;
    }
};


class OK: public Response
{
   
 public:
    OK(const std::string &body): Response("HTTP/1.1 200 OK", body){}
    //OK(std::ostringstream &&body): Response("200 OK", std::move(body)){}
 
};

class NotFound: public Response
{
public:
    NotFound(const std::string &body): Response("HTTP/1.1 404 Not Found", body){}
    //NotFound(std::ostringstream &&body): Response("404 Not Found", std::move(body)){}

};


class ServerError: public Response
{
public:
    ServerError(const std::string &body): Response("HTTP/1.1 500 Error", body){}
    //ServerError(std::ostringstream &&body): Response("500 Error", std::move(body)){}

};
