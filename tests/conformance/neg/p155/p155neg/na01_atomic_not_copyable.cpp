#include <atomic>
int main() { std::atomic<int> a(1); std::atomic<int> b(a); return b; }
