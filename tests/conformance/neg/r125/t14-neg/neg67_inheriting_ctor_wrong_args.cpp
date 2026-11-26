struct B { B(int) {} };
struct D : B { using B::B; };
int main() { D d("str"); return 0; }
