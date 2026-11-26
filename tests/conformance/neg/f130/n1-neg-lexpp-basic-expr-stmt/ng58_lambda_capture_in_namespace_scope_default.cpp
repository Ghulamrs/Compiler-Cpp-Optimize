int g = 1; int main() { auto l = [g] { return g; }; return l(); }
