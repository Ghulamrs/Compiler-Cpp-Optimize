// A comment after the name of #ifdef or #ifndef is whitespace, as it is on any directive line:
// it used to be read as part of the name, so every such #ifndef was taken.
#include <cstdio>
#define KNOWN 1

int main()
{
#ifndef KNOWN /* a block comment */
    std::printf("wrong: #ifndef KNOWN with a block comment\n");
#endif
#ifdef KNOWN // a line comment
    std::printf("#ifdef KNOWN with a line comment\n");
#endif
#ifdef NEVER_DEFINED /* a comment */
    std::printf("wrong: #ifdef of an undefined name\n");
#endif
#if KNOWN /* and #if, as before */
    std::printf("#if KNOWN\n");
#endif
    return 0;
}
