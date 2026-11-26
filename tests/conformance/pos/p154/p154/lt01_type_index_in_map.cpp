#include <typeindex>
#include <cstdio>
#include <map>
#include <string>
struct A {}; struct B {};
int main() {
    std::map<std::type_index, std::string> m;
    m[typeid(int)] = "int";
    m[typeid(A)] = "A";
    m[std::type_index(typeid(B))] = "B";
    std::printf("%s %s %s %d\n", m[typeid(int)].c_str(), m[typeid(A)].c_str(), m[typeid(B)].c_str(), (int)m.size());
    std::type_index a(typeid(A)), b(typeid(B));
    std::printf("%d %d %d\n", (int)(a == a), (int)(a != b), (int)((a < b) != (b < a)));
    std::printf("%d\n", (int)(std::hash<std::type_index>()(a) == a.hash_code()));
    return 0;
}
