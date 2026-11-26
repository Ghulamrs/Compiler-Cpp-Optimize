union U { int i; float f; int get() const { return i; } };
int main() { U u; u.i = 3; return u.get() == 3 ? 0 : 1; }
