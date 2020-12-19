#include <stdexcept>
#include <errno.h>
#include <cstring>

#include "util.h"

int throw_on_err(int result)
{
    if(result == -1) throw std::runtime_error(strerror(errno));
    return result;
}

