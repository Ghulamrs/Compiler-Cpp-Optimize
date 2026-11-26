#include <cstdio>
#include <new>
#include <cstddef>
void nh() {}
int main() { std::new_handler old = std::set_new_handler(nh); std::new_handler cur = std::get_new_handler(); std::set_new_handler(old); int *p = new (std::nothrow) int(3); void *raw = ::operator new(16); ::operator delete(raw); void *rawa = ::operator new[](16, std::nothrow); ::operator delete[](rawa, std::nothrow); alignas(int) char buf[sizeof(int)]; int *q = new (buf) int(4); std::bad_alloc ba; std::bad_array_new_length bl; const std::bad_alloc &rb = bl;
std::printf("%d %d %d %d %d\n", (int)(cur == nh), *p, *q, (int)(ba.what() != nullptr), (int)(rb.what() != nullptr)); delete p; return 0; }
