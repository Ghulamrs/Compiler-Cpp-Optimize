// [expr.add]/6: the difference of two pointers is of the type <cstddef> calls std::ptrdiff_t - whichever
// integer that is on the target. Until 1.68 the C6747 gave `long` where its ptrdiff_t is `int`: as wide,
// another type, so decltype, overload resolution and a template argument each saw the wrong one.
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <type_traits>

int which(int) { return 1; }
int which(long) { return 2; }
int which(long long) { return 3; }
int which(std::ptrdiff_t, const char *) { return 9; }

template <class T> struct Is { static int of(std::ptrdiff_t) { return 0; } };
template <class T> int deduced(T) { return std::is_same<T, std::ptrdiff_t>::value ? 1 : 0; }

int main() {
    int a[10];
    int *p = a + 2, *q = &a[7];
    const char *s = "abcdef", *e = s + 6;
    std::ptrdiff_t d = q - p;
    // the same overload for a difference as for a ptrdiff_t itself, whatever integer that is
    const int same = which(q - p) == which(d) ? 1 : 0;
    std::printf("%d %d\n", (int)d, (int)(e - s));
    std::printf("%d %d %d\n", (int)std::is_same<decltype(q - p), std::ptrdiff_t>::value,
                (int)std::is_same<decltype(e - s), std::ptrdiff_t>::value, same);
    std::printf("%d %d\n", deduced(q - p), (int)std::is_same<std::iterator_traits<int *>::difference_type, decltype(q - p)>::value);
    std::printf("%d\n", (int)(sizeof(q - p) == sizeof(std::ptrdiff_t) && sizeof(std::ptrdiff_t) == sizeof(void *)));
    return 0;
}
