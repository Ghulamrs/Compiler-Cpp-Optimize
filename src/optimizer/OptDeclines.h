#pragma once

// **Where the optimizer holds back, said out loud when CPP11_DECLINES is set**: one line on stderr
// per function or call site a transformation declines, `decline <reason> <symbol>`. Unset, it costs
// one cached getenv and prints nothing; nothing emitted depends on it.

#include <cstdio>
#include <cstdlib>
#include <string>

namespace opt {

inline bool reportingDeclines() {
    static const bool on = std::getenv("CPP11_DECLINES") != nullptr;
    return on;
}

inline void noteDecline(const char *reason, const std::string &where) {
    if (reportingDeclines()) std::fprintf(stderr, "decline %s %s\n", reason, where.c_str());
}

}
