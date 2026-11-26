template <class T> struct X { static int f() { return 1; } }; int a = X<int>::f(); template <> struct X<int> {}; int main() { return a; }
