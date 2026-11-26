// A mildly heavy workload: 200k string appends, a vector of 100k ints sorted, a map of 50k entries - correctness of results under both -O levels (and timings recorded by the runner).
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
int main() {
    std::string s; for (int i = 0; i < 200000; ++i) s += (char)('a' + i % 26); std::printf("%d %c %c\n", (int)s.size(), s[0], s[199999]);
    std::vector<int> v; for (int i = 0; i < 100000; ++i) v.push_back((i * 1103515245 + 12345) & 0x7FFFFFFF); std::sort(v.begin(), v.end()); std::printf("%d %d\n", v[0] <= v[1], v[99999] >= v[99998]);
    std::map<int, int> m; for (int i = 0; i < 50000; ++i) m[(i * 7) % 50000] = i; std::printf("%d %d\n", (int)m.size(), m[49993]);
    long long h = 0; for (int i = 0; i < 100000; ++i) h = h * 31 + v[i]; std::printf("%lld\n", h);
    return 0;
}
