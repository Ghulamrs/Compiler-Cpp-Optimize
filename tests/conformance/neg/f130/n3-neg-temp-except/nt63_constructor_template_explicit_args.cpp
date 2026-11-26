struct S { template <class T> S(T) {} }; int main() { S s<int>(1); return 0; }
