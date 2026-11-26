#include <cstdio>
class S { int priv[4]; static const int K = 3; public: static const int Kp = K; int n = K; };
int main() { std::printf("%d %d %d\n", (int)sizeof(S), S::Kp, S().n); return 0; }
