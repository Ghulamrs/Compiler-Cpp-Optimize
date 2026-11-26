#include <cstdio>
#include <new>
int main() { int n = -1; try { int *p = new int[n]; std::printf("no throw %p\n", (void*)p); } catch (std::bad_array_new_length &) { std::printf("bad_array_new_length\n"); } catch (std::bad_alloc &) { std::printf("bad_alloc\n"); } return 0; }
