#pragma once
#include <sstream>
#include <string>

//
// HTTP response including a header and body
//
// Right now the response and body are just strings, but in future I would like
// to allow streaming responses
//
class Response
{
    std::string m_header;
    std::string m_body;
    
public:
    Response(const std::string &header, const std::string &body):
        m_header(header), m_body(body)
    {}

    virtual ~Response(){}

    operator std::string()
    {
        return m_header + "\r\n\r\n" + m_body;
    }
};


//
// 200 OK response
//
class OK: public Response
{
   
 public:
    OK(const std::string &body): Response("HTTP/1.1 200 OK", body){}
};


//
// 404 Not found 
//
class NotFound: public Response
{
public:
    NotFound(const std::string &body): Response("HTTP/1.1 404 Not Found", body){}
};


//
// 500 Error
//
class ServerError: public Response
{
public:
    ServerError(const std::string &body): Response("HTTP/1.1 500 Error", body){}
};
