template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
template <class T> struct is_int { static const bool value = false; };
template <> struct is_int<int> { static const bool value = true; };
template <class T> typename enable_if<is_int<T>::value, int>::type f(T) { return 1; }
template <class T> typename enable_if<!is_int<T>::value, int>::type f(T) { return 2; }
int main() { return f(1) + f(1.0) == 3 ? 0 : 1; }
