int main() { goto inside; try { inside: return 1; } catch (...) {} return 0; }
