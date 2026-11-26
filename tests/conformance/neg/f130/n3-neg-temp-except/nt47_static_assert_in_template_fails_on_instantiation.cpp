template <class T> struct X { static_assert(sizeof(T) == 1, "size"); }; int main() { X<int> x; return 0; }
