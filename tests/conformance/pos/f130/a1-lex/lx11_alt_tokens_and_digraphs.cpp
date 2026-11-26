#include <cstdio>
int main() { int a<:2:> = <%1, 2%>; bool b = a[0] < a[1] and not (a[0] == a[1]) or false; int c = 6; c xor_eq 3; c bitand 1;
%:define X(y) %:y
std::printf("%d %s %d\n", (int)b, X(hi), c); return 0; }
