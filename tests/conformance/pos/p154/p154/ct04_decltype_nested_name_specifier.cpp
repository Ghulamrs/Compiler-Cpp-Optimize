template <class T> struct P { typedef P<T> period; static const long long den = 7; };
int main() { P<int> c; auto a = decltype(c)::den; long long b = (long long)decltype(c)::period::den; return (int)(a + b) - 14; }
