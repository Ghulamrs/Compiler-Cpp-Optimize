// [over.over]/1: a function template named without a call where no function type says which specialization.
template <class T> T twice(T t) { return t + t; }
int main() { auto p = twice; return p(1); }
