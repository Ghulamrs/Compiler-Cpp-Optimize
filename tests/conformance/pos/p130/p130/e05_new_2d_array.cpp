int main() { int n = 2; int (*p)[3] = new int[n][3]; p[1][2] = 7; int r = p[1][2]; delete[] p; return r == 7 ? 0 : 1; }
