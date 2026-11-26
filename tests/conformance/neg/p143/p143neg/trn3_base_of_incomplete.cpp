// [meta.rel]/2: is_base_of<B, D> needs D complete when both are distinct classes.
struct B {};
struct D;
int main() { return __is_base_of(B, D); }
