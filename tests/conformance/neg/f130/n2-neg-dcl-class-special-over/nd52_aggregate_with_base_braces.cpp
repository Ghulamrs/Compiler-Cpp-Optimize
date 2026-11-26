struct B { int a; }; struct D : B { int b; }; int main() { D d = {1, 2}; return d.b; }
