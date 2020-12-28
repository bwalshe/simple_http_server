#include "connection_processor.h"

void ConnectionProcessor::process(TcpConnectionQueue::connection_ptr connection)
{
    try
    {
        auto r = parse_request(connection);
        if(r.has_value())
        {
            std::shared_ptr<Request> request(std::make_shared<Request>(std::move(r.value())));
            for(auto handler: m_handlers)
            {
                if(handler->matches(r.value()))
                {
                    m_arena.enqueue([=]{handler->process(std::move(*request));});
                    return;
                }
            }
            m_arena.enqueue([=]{request->respond(m_not_found_response(*request));});
        }
    }
    catch (const std::runtime_error& e)
    {
        std::cerr << e.what() << std::endl;
        connection->respond(m_error_response());  // something has gone wrong, respond directly in this thread.
    }
}

