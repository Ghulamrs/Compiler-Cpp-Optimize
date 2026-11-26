template <class T> int f() { return sizeof...(T); } int main() { return f<int>(); }
