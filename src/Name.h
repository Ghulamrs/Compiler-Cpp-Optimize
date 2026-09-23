#pragma once

// **The program's name, once**: what it calls itself in messages and temporary
// files, and the prefix of every variable it reads (CPP11_AS, CPP11_LD, ...). The
// Makefile's PROGRAM and RIDE's tools/make-projects.py spell it the same.

#include <string>

namespace program {

constexpr const char *kName = "cpp11";

// The environment variable named for this program: env("AS") is CPP11_AS.
inline std::string env(const char *what) { return std::string("CPP11_") + what; }

}
