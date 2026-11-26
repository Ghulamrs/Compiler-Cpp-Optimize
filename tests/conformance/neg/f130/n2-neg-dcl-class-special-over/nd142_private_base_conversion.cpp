struct B {}; struct D : private B {}; int main() { D d; B *b = &d; return 0; }
