// Delegation idioms seen in real code: a lambda stored in a local and called twice, a lambda returning a reference to a member.
#include <cstdio>
struct Acc { int total = 0; void add(int k) { auto inc = [this](int x) { total += x; }; inc(k); inc(k); } int &ref() { auto r = [this]() -> int & { return total; }; return r(); } };
int main() { Acc a; a.add(5); a.ref() += 1; std::printf("%d\n", a.total); return 0; }
