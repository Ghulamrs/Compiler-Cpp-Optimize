// [temp.mem]/2: a local class has no member templates.
int main() { struct L { template <class T> struct In { T t; }; }; return 0; }
