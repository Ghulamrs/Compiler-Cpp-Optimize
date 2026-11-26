int main() { volatile int v = 1; int *p = static_cast<int *>(&v); return *p; }
