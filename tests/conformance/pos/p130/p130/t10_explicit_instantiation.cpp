template <class T> struct Box { T v; T get() const { return v; } };
template struct Box<int>;
extern template struct Box<long>;
template <class T> T twice(T t) { return t + t; }
template int twice<int>(int);
int main() { Box<int> b; b.v = 1; return b.get() == 1 && twice(2) == 4 ? 0 : 1; }
