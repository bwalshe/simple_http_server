#pragma once
#include <signal.h>
#include <initializer_list>


//
// For use with linux system calls. Call the funciton and if it returns -1,
// throw a std::runtrime_error error based on `strerror(errno)` if the result
// is not -1, then return it unaltered.
//
int throw_on_err(int result, const char* where);


//
// Turn off the default action for the specified signals and return a sgset_t
// containing the signal types that were turned off
sigset_t block_signals();//std::initializer_list<int> signals);

