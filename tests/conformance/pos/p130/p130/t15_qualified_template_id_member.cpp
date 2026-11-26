template <class T> struct W { static const int n = sizeof(T); typedef T type; };
int main() { W<int>::type x = W<int>::n; return x == 4 ? 0 : 1; }
