#include <sstream>
#include <stdexcept>
#include <errno.h>
#include <cstring>
#include <ostream>
#include "util.h"

int throw_on_err(int result, const char* where)
{
    if(result == -1)
    {
        std::ostringstream msg;
        msg << where << ":\n" << strerror(errno);
        throw std::runtime_error(msg.str());
    }
    return result;
}

sigset_t block_signals()
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGINT);
    throw_on_err(sigprocmask(SIG_BLOCK, &mask, NULL), "block SIGINT/SIGQUIT");
    return mask;
}


