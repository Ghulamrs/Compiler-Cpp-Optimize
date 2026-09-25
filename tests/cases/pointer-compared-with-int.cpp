// A pointer compares with a pointer or with a null pointer constant, not with
// an integer (cl C2446, the cl review's A26).
int main() { int *p = 0; if (p == 1) return 1; return 0; }
