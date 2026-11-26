// Common library combination: map<string,int> filled from a vector<string>, iterated in order, erased while iterating with the returned iterator.
#include <cstdio>
#include <map>
#include <string>
#include <vector>
int main() {
    std::vector<std::string> words = {"pear", "apple", "fig", "apple", "pear", "apple"};
    std::map<std::string, int> count; for (const std::string &w : words) ++count[w];
    for (std::map<std::string, int>::iterator it = count.begin(); it != count.end(); ) { if (it->second == 1) it = count.erase(it); else ++it; }
    for (const std::pair<const std::string, int> &p : count) std::printf("%s=%d ", p.first.c_str(), p.second); std::printf("\n");
    return 0;
}
