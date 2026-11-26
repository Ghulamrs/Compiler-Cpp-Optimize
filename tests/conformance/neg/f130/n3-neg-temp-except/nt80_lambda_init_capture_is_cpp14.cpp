int main() { auto l = [x = 1] { return x; }; return l(); }
