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
    virtual void process(Request &&request) = 0;
    virtual ~RequestHandler(){}
};

class ConnectionProcessor
{
public:
    class Builder
    {
        bool error_set = false;
        bool missing_set = false;
        int m_num_threads;
        std::vector<std::shared_ptr<RequestHandler>> m_handlers;
        std::function<Response(void)> m_error_response;
        std::function<Response(Request&)> m_not_found_response;

        public:

        Builder *with_threads(int num_threads)
        {
            m_num_threads = num_threads;
            return this;
        }

        Builder *with_not_found_response(std::function<Response(Request&)> &&f)
        {
            missing_set = true;
            m_not_found_response = f;
            return this;
        }

        Builder *with_error_response(std::function<Response()> &&f)
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

        ConnectionProcessor build()
        {
            if(!error_set)
            {
                throw std::runtime_error("No error response set");
            }
            if(!missing_set)
            {
                throw std::runtime_error("No missing page response set");
            }
            return ConnectionProcessor(m_num_threads, std::move(m_handlers), std::move(m_not_found_response), 
                    std::move(m_error_response));
        }

    };
   
    oneapi::tbb::task_arena m_arena;
    std::vector<std::shared_ptr<RequestHandler>> m_handlers;
    std::function<Response(Request&)> m_not_found_response;
    std::function<Response(void)> m_error_response;

public:
    ConnectionProcessor(int threads, std::vector<std::shared_ptr<RequestHandler>> &&handlers, 
            std::function<Response(Request&)> &&not_found_response, 
            std::function<Response(void)> &&error_response):
        m_arena(threads),
        m_handlers(std::move(handlers)),
        m_not_found_response(not_found_response),
        m_error_response(error_response){}

    void process(TcpConnectionQueue::connection_ptr connection);

    static Builder builder()
    { 
        return Builder();
    }
};


