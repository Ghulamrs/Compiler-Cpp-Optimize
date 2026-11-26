struct S { int v; static int get() { return this->v; } };
int main() { return S::get(); }
