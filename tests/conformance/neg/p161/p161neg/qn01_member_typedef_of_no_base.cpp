// [class.qual]: the class named is the object's or a base of it.
struct Other { int f() { return 1; } };
struct D { typedef Other base; int f() { return 2; } };
int main() { D d; return d.base::f(); }
