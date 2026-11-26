template <class T> int f(T) { return 1; } int x = f(1); template <> int f(int) { return 2; } int main() { return 0; }
