struct F { unsigned a : 3; };
int main() { F f; f.a = 1; f.a++; return f.a == 2 ? 0 : 1; }
