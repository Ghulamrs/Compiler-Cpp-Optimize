// [temp.res]/10: g(1) depends on nothing, so g must be declared where the template is written.
template <class T> int f(T) { return nothere(1); }
int main() { return 0; }
