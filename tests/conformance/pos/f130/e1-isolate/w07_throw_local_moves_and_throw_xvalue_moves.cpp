#include <cstdio>
#include <utility>
struct E { E() {} E(E &&) { std::printf("mv "); } E(const E &) { std::printf("cp "); } };
int main() { try { E e; throw e; } catch (E &) {} try { E e; throw std::move(e); } catch (E &) {} std::printf("\n"); return 0; }
