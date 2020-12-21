#pragma once

//
// For use with linux system calls. Call the funciton and if it returns -1,
// throw a std::runtrime_error error based on `strerror(errno)` if the result
// is not -1, then return it unaltered.
//
int throw_on_err(int result, const char* where);

