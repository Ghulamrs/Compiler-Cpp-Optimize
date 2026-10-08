#include <cstdio>
// [temp.res]/9: a non-dependent name in a template body is looked up where the
// template is defined, and `later` is declared only afterwards - so clang
// refuses the call (5:42, "neither visible in the template definition nor
// found by argument-dependent lookup"; `int` has no associated namespace for
// ADL to reach it by). cxx1 instantiates by replaying the template's tokens and
// looks every name up at the instantiation, MSVC's old model, so it finds
// `later` and prints 7. That is the decision CLAUDE.md "Decisions already
// taken" records, kept here so the over-acceptance is counted rather than
// described. The oracle must be asked with -fno-delayed-template-parsing on a
// Windows-hosted clang, whose MSVC default turns two-phase lookup off.
template <class T> int use(T t) { return later(t); }
int later(int n) { return n + 4; }
int main() { std::printf("%d\n", use(3)); return 0; }
