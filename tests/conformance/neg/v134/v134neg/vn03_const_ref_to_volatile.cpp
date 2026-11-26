int main() { volatile int v = 1; const int &r = v; return r; }
