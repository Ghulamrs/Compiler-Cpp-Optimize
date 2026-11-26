int main() { int (*p)() = &main; return p != 0; }
