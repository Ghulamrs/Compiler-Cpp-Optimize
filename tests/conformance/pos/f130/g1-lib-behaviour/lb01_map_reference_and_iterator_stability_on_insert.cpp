#include <cstdio>
#include <map>
#include <set>
#include <type_traits>
int main() { std::map<int, int> m; m[50] = 1; int *ref = &m[50]; std::map<int, int>::iterator it = m.find(50); for (int i = 0; i < 2000; i++) m[i] = i; std::set<int> s; s.insert(5); const int *sref = &*s.begin(); for (int i = 100; i < 3000; i++) s.insert(i);
std::printf("%d %d %d %d %d\n", (int)(ref == &m[50]), (int)(it->first == 50 && &it->second == &m[50]), (int)(sref == &*s.find(5)), (int)std::is_pointer<std::map<int, int>::iterator>::value, (int)std::is_pointer<std::set<int>::iterator>::value); return 0; }
