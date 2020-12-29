#include "request_processor.h"
#include <future>
#include <memory>

std::shared_ptr<Response> RequestProcessor::process(const Request &request)
{
    for(auto handler: m_handlers)
    {
        if(handler->matches(request))
        {
            return handler->process(request);
        }
    }
    return std::make_shared<Response>(m_not_found_response(request));
}
