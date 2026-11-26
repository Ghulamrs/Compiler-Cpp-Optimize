namespace long_name { int v = 2; }
int main() { namespace ln = long_name; return ln::v == 2 ? 0 : 1; }
