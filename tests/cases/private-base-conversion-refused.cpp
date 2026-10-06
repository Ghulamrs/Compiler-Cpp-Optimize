// [class.access.base]/4: outside the class and its friends a private base is not
// accessible, so Hidden * does not become A *. clang refuses it.
struct A { virtual ~A() {} int a; };
struct Hidden : private A { };
int main() { Hidden h; A *p = &h; return p != 0; }
