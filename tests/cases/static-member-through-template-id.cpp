// A static data member named through the template's argument list, at the
// start of a statement too: `Box<int>::count = 3;` is an assignment, not a
// declaration of nothing (the cl review's A23, then A13).
extern "C" int printf(const char *, ...);
template <class T> struct Box { static int count; };
template <class T> int Box<T>::count = 0;
int main() { Box<int>::count = 3; Box<int>::count++; printf("%d\n", Box<int>::count); return 0; }
