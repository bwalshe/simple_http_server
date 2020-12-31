#pragma once
#include <memory>
#include <oneapi/tbb/task_arena.h>
#include <stdexcept>
#include "connection.h"
#include "request.h"


class RequestHandler
{
public:
    virtual bool matches(const Request &request) = 0;
    virtual std::shared_ptr<Response> process(const Request &request) = 0;
    virtual ~RequestHandler(){}
};

class RequestProcessor
{
    using response_ptr = std::shared_ptr<Response>;
public:
    class Builder
    {
        bool error_set = false;
        bool missing_set = false;
        std::vector<std::shared_ptr<RequestHandler>> m_handlers;
        std::function<ServerError(void)> m_error_response;
        std::function<NotFound(const Request&)> m_not_found_response;

        public:

        Builder *with_not_found_response(std::function<NotFound(const Request&)> &&f)
        {
            missing_set = true;
            m_not_found_response = f;
            return this;
        }

        Builder *with_error_response(std::function<ServerError(void)> &&f)
        {
            error_set = true;
            m_error_response = f;
            return this;
        }

        Builder *with_request_handler(RequestHandler *handler)
        {
            m_handlers.push_back(std::shared_ptr<RequestHandler>(handler));
            return this;
        }

        RequestProcessor build()
        {
            if(!error_set)
            {
                throw std::runtime_error("No error response set");
            }
            if(!missing_set)
            {
                throw std::runtime_error("No missing page response set");
            }
            return RequestProcessor(std::move(m_handlers), std::move(m_not_found_response), 
                    std::move(m_error_response));
        }

    };
   
    std::vector<std::shared_ptr<RequestHandler>> m_handlers;
    std::function<NotFound(const Request&)> m_not_found_response;
    std::function<ServerError(void)> m_error_response;

public:
    RequestProcessor(std::vector<std::shared_ptr<RequestHandler>> &&handlers, 
            std::function<NotFound(const Request&)> &&not_found_response, 
            std::function<ServerError(void)> &&error_response):
        m_handlers(std::move(handlers)),
        m_not_found_response(not_found_response),
        m_error_response(error_response){}

    response_ptr process(const Request&);

    void respond(std::shared_ptr<TcpConnectionQueue::IncomingConnection> connection)
    {
        auto request = parse_request(connection);
        if(request.has_value())
        {
            connection->respond([=]{return process(request.value());});
        }
    }

    static Builder builder()
    { 
        return Builder();
    }
};


