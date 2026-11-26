#pragma once

// **Where the optimizer holds back, said out loud when CPP11_DECLINES is set**: one line on stderr
// per function or call site a transformation declines, `decline <reason> <symbol>`. Unset, it costs
// one cached getenv and prints nothing; nothing emitted depends on it.
//
// **Empty or 0 is unset** (1.10, 2026-10-20). The IDEs pass `CPP11_DECLINES=` for the option off,
// which cmd's `set NAME=` used to remove; GRIDE starts cpp11 itself now, so the empty variable
// arrived, and every compile of a build reported every decline.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace opt {

inline bool reportingDeclines() {
    static const bool on = [] {
        const char *v = std::getenv("CPP11_DECLINES");
        return v != nullptr && *v != '\0' && std::strcmp(v, "0") != 0;
    }();
    return on;
}

inline void noteDecline(const char *reason, const std::string &where) {
    if (reportingDeclines()) std::fprintf(stderr, "decline %s %s\n", reason, where.c_str());
}

}
