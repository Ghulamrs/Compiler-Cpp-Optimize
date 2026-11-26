struct B { int v; };
struct D : B {};
void take(B &&) {}
int main() {
    D d;
    take(d);
    return 0;
}
