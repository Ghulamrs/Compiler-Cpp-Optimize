#include <cstdio>
int main() { char s[] = {"ab"}; const char t[3] = {"xy"}; std::printf("%s %s %d\n", s, t, (int)sizeof(s)); return 0; }
