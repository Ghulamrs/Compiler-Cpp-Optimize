struct B {}; struct D : B {};
int main() { const D d{}; B &b = static_cast<B &>(d); return 0; }
