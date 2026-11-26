#include <cstdio>
struct final { int override; }; int main() { final f; f.override = 3; int final = 2; std::printf("%d\n", f.override + final); return 0; }
