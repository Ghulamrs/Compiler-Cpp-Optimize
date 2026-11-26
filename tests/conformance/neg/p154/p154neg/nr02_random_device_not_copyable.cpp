#include <random>
int main() { std::mt19937 a; std::mt19937 b = a; std::random_device d; std::random_device e = d; return (int)(a == b); }
