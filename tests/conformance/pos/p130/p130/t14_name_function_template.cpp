template <class T> T tw(T t) { return t * 2; }
int main() { int (*p)(int) = tw; int (*q)(int) = &tw<int>; return p(2) + q(1) == 6 ? 0 : 1; }
