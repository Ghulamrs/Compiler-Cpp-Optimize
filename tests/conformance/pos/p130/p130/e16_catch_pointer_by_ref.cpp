int main() { int i = 0; try { throw &i; } catch (int *&p) { return p == &i ? 0 : 1; } return 1; }
