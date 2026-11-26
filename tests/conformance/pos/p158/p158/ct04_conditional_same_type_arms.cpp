#include <cstdio>

// [expr.cond]/6: two arms of one type give that type - a char and a character literal are a char, so the
// braces take them without narrowing, and sizeof sees a char.
template <class T, class U> struct Same { static const bool value = false; };
template <class T> struct Same<T, T> { static const bool value = true; };
template <class T> bool isChar(T) { return Same<T, char>::value; }

int main(int argc, char **) {
    const char x = 'q';
    char a[4] = { argc != 0 ? x : '\0', argc != 0 ? 'b' : 'c', argc == 0 ? x : 'z' };
    short s = 3;
    const short t = argc != 0 ? s : static_cast<short>(4);
    std::printf("%s %d %d %d\n", a, (int)sizeof(argc != 0 ? x : 'y'), (int)isChar(argc != 0 ? x : 'y'), t);
    return 0;
}
