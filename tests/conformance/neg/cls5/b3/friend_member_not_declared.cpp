struct B { int peek(); };
class A { int v = 1; friend int B::look(); };
int main() { return 0; }
