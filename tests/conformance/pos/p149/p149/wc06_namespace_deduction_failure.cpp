// [temp.deduct]/8 inside a namespace: a function template whose deduction fails only drops out, and the
// namespace's ordinary overload answers - the sizeof idiom of type_traits, written in a namespace.
#include <cstdio>
namespace ns {
template <class U, int M> char bt(U (&)[M]);
long bt(...);
template <class T> struct IsArray { static const bool value = sizeof(bt(*static_cast<T *>(0))) == 1; };
template <class U> char pt(U *);
long pt(...);
template <class T> struct IsPtr { static const bool value = sizeof(pt(*static_cast<T *>(0))) == 1; };
namespace deeper { template <class T> struct Both { static const bool value = IsArray<T>::value || IsPtr<T>::value; }; }
}
int main() {
    std::printf("%d %d %d %d %d %d\n", (int)ns::IsArray<int>::value, (int)ns::IsArray<int[3]>::value,
                (int)ns::IsPtr<int>::value, (int)ns::IsPtr<int *>::value, (int)ns::deeper::Both<char>::value,
                (int)ns::deeper::Both<char *>::value);
    return 0;
}
