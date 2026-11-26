// [over.ics.rank]/3.3 with /3.2.3: two user-defined conversion sequences through the same constructor, the one binding an rvalue reference to the temporary wins.
#include <cstdio>
#include <string>
struct S { S(const char *) {} };
const char *g(const std::string &) { return "const&"; }
const char *g(std::string &&) { return "&&"; }
const char *h(const S &) { return "const&"; }
const char *h(S &&) { return "&&"; }
int main() { std::printf("%s %s\n", g("lit"), h("lit")); return 0; }
