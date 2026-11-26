#include <cstdio>
#include <new>
#include <cstddef>
static volatile int shift = 62;
int main() { try { char *p = new char[(std::size_t)1 << shift]; std::printf("no throw %p\n", (void*)p); } catch (std::bad_alloc &) { std::printf("bad_alloc\n"); } return 0; }
