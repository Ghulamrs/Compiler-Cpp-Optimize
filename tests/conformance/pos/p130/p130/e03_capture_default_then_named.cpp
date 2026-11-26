int main() { int a = 1, b = 2; auto l = [=, &b]() { b = a + b; }; l(); return b == 3 ? 0 : 1; }
