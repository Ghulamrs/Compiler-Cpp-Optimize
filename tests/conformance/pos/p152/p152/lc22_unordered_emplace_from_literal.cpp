#include <cstdio>
#include <string>
#include <unordered_map>
#include <utility>
int main() {
    setvbuf(stdout, 0, _IONBF, 0);
    std::unordered_map<std::string, int> m;
    std::pair<std::unordered_map<std::string, int>::iterator, bool> r = m.emplace("new", 5);
    std::printf("a %d %d\n", (int)r.second, r.first->second);
    r = m.emplace(std::string("new"), 6);
    std::printf("b %d %d\n", (int)r.second, r.first->second);
    r = m.emplace("new", 7);
    std::printf("c %d %d\n", (int)r.second, r.first->second);
}
