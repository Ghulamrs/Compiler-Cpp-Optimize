#include <cstdio>
#include <string>
struct Cnt { static int copies; Cnt() {} Cnt(const Cnt &) { copies++; } Cnt(Cnt &&) {} }; int Cnt::copies = 0;
int main() { int arr[3] = {1, 2, 3}; Cnt c; std::string s = "abc"; auto l = [arr, c, s] { return arr[0] + arr[2] + (int)s.size(); }; arr[0] = 100; std::printf("%d %d %d\n", l(), Cnt::copies, (int)sizeof(l) >= 12 + (int)sizeof(std::string)); return 0; }
