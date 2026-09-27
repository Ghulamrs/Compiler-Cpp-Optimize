#pragma once

// The arm64 optimizer over one function body's emitted text, as c6xSchedule is the C6000's: the -O0 code
// is a stack machine, so a push its pop follows in the same straight run keeps its value in a register.

#include <string>
#include <vector>

// The body with its -O0 shapes rewritten; level 0 hands it back as it came. With `saved`, locals may move into
// callee-saved registers, each named there for the caller to save in the prologue and restore at the return.
// resultInX: the function returns a value in x0-x7 (false for void or a floating result).
std::string a64Peephole(const std::string &text, int level, std::vector<std::string> *saved = nullptr,
                        bool resultInX = true);
