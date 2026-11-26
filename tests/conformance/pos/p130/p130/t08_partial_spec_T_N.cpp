template <class T> struct ext { static const int value = 0; };
template <class T, int N> struct ext<T[N]> { static const int value = N; };
int main() { return ext<int[7]>::value == 7 ? 0 : 1; }
