struct H { int v[3]; H() : v{1, 2, 3} {} };
int main() { H h; return h.v[0] + h.v[2] == 4 ? 0 : 1; }
