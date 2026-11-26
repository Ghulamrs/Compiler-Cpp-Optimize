volatile int v = 1;
int main() { volatile int *p = &v; *p = 2; return v == 2 ? 0 : 1; }
