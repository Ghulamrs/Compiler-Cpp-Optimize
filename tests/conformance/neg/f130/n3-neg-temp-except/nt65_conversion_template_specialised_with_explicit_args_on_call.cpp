struct S { template <class T> operator T() const; }; int main() { S s; return s.operator int<int>(); }
