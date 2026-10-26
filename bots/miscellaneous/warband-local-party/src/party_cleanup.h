#pragma once
// SDK-free release boundary: neither cleanup obligation may suppress the other.
#include <exception>

namespace mb::party {

template<class Release, class Restore>
void finish_release(Release release, Restore restore) {
    std::exception_ptr first;
    try {
        release();
    } catch (...) {
        first = std::current_exception();
    }
    try {
        restore();
    } catch (...) {
        if (!first) first = std::current_exception();
    }
    if (first) std::rethrow_exception(first);
}

} // namespace mb::party
