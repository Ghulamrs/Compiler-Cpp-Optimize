int main() { int a = 1; auto l = [a, a] { return a; }; return l(); }
