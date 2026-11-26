class C { int secret; public: C() : secret(1) {} };
int main() { C c; return c.secret; }
