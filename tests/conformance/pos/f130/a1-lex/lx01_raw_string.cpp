#include <cstdio>
int main() { const char *s = R"(a\n"b")"; const char *t = R"xy(c)"d)xy"; std::printf("%s|%s|%d\n", s, t, (int)sizeof(R"(
)")); return 0; }
