int main() { int a = 1; auto l = [=, a]() { return a; }; return l(); }
