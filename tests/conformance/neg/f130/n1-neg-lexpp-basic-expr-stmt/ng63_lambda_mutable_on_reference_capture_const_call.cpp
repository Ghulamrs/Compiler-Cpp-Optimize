int main() { int a = 1; auto l = [a]() { a = 2; return a; }; return l(); }
