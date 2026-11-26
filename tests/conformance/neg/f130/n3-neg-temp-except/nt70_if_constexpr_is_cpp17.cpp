template <class T> int f(T) { if constexpr (sizeof(T) == 4) return 1; else return 2; } int main() { return f(1); }
