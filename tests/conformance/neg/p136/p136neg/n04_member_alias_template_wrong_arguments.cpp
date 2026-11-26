// [temp.arg]/1: one parameter, two arguments.
struct S { template <class T> using M = T; };
int main() { S::M<int, int> m = 4; return m; }