#include <cstdio>
#include <vector>
#include <string>
#include <utility>
int main() { std::vector<bool> vb(70, true); vb[65] = false; std::string a(100, 'x'); std::string b = std::move(a); std::printf("%d %d %d %d %d\n", (int)vb.size(), (int)vb[65], (int)vb[64], (int)(sizeof(std::vector<bool>) <= 4 * sizeof(void*)), (int)(b.size() == 100)); return 0; }
