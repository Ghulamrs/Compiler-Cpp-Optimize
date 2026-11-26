struct Outer { template <class T> struct In { T v; }; };
int main() { Outer::In<int> i; i.v = 2; return i.v == 2 ? 0 : 1; }
