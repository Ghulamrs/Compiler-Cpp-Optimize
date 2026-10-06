// [class.access.base]/5: a member of a private base is a private member of the class
// that inherits it, and a class derived from that one cannot name it. clang refuses it.
struct A { int a; };
struct Hidden : private A { };
struct Further : Hidden { int get() { return a; } };
int main() { Further f; return f.get(); }
