// C++11 restricts a constexpr function body to a single return; C++14 relaxed it. clang -std=c++11 rejects, cl (C++14) accepts.
constexpr int f(int x) { if (x > 0) return 1; return 2; }
int main() { return f(1); }
