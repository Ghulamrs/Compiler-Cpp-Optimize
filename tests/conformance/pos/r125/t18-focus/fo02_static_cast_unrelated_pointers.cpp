// static_cast<int*>(double*) is ill-formed; cpp11 accepted it in the negative suite. Here the accepted program is run to show what it does.
#include <cstdio>
int main() { double d = 1.0; void *v = &d; int *ok = static_cast<int *>(v); std::printf("%d\n", ok == v); return 0; }
