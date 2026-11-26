template <bool B> struct E { static const int v = B; };
template <long N, class = E<(N < 3)>> struct L { static const long v = 2; };
int main() { return L<1>::v - 2; }
