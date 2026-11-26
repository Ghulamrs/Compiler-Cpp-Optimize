// [temp.class.spec.match]: `K<T *>` does not match `int *const`, so the undefined primary is used and is incomplete.
template <class T> struct K;
template <class T> struct K<T *> { static const int v = 1; };
int main() { return K<int *const>::v; }
