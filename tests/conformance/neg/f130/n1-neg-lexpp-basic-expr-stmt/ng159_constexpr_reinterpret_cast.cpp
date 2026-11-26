int main() { constexpr int x = 1; constexpr const int *p = &x; constexpr long l = reinterpret_cast<long>(p); return (int)l; }
