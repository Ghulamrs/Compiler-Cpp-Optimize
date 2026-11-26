// [temp.res]/3: `typename EI<true, int>::type` names a member that is not a type, so the parameter has none.
template <bool B, class T = void> struct EI { static const int type = 0; };
template <class T, typename EI<true, T>::type = 0> int f(T) { return 1; }
int main() { return f(1); }
