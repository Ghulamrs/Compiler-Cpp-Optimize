#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
int main() {
    setvbuf(stdout, 0, _IONBF, 0);
    std::unordered_set<std::string> s;
    s.emplace("set");
    std::printf("a %d\n", (int)s.size());
    std::unordered_map<std::string, int> m;
    m.insert(std::pair<const std::string, int>("x", 1));
    std::printf("b %d\n", (int)m.size());
    m.emplace(std::pair<const std::string, int>("y", 2));
    std::printf("c %d\n", (int)m.size());
    m.emplace(std::string("z"), 3);
    std::printf("d %d\n", (int)m.size());
    m.emplace("w", 4);
    std::printf("e %d\n", (int)m.size());
}
