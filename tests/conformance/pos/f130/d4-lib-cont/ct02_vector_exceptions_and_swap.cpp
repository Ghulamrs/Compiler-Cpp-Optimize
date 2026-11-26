#include <cstdio>
#include <vector>
#include <stdexcept>
#include <utility>
struct T { static int live; int v; T(int v) : v(v) { live++; } T(const T &o) : v(o.v) { live++; } ~T() { live--; } }; int T::live = 0;
int main() { std::vector<int> v{1, 2}; try { v.at(5); } catch (std::out_of_range &) { std::printf("oor "); } { std::vector<T> t; t.push_back(T(1)); t.push_back(T(2)); t.erase(t.begin()); std::printf("%d %d ", T::live, t[0].v); } std::printf("%d ", T::live); std::vector<int> a{1}, b{2, 3}; a.swap(b); std::swap(a, b); std::printf("%d %d ", (int)a.size(), (int)b.size()); const std::vector<int> c{7}; std::printf("%d %d\n", c.at(0), c.front()); return 0; }
