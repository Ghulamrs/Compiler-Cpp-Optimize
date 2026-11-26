// const auto & and auto & from const objects, parameters and returns.
#include <cstdio>
const int gc = 5;
int sum(const int &a, const int &b) { const auto &x = a; auto &y = b; const auto z = a; return x + y + z; }
int main() { const auto &r = gc; auto &s = gc; const auto &t = sum(1, 2); std::printf("%d %d %d\n", r, s, t); return 0; }
