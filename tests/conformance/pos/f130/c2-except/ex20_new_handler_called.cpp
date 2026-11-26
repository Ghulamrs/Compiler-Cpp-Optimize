#include <cstdio>
#include <cstdlib>
#include <new>
#include <cstddef>
static volatile int shift = 62; void nh() { std::printf("new_handler\n"); std::exit(5); }
int main() { std::set_new_handler(nh); char *p = new char[(std::size_t)1 << shift]; std::printf("no %p\n", (void*)p); return 0; }
