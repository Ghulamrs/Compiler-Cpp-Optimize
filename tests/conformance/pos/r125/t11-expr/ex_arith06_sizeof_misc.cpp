// sizeof of expressions (no evaluation), types, arrays, references (size of referent), classes with padding, empty class, function pointer, member pointers; alignof; sizeof(void*) == 8; sizeof of unevaluated function call with side effects.
#include <cstdio>
struct E {}; struct P { char c; int i; }; struct Q { char c; double d; char e; }; struct Vt { virtual void f() {} };
int effect() { std::printf("evaluated! "); return 1; }
struct Big { char b[1000]; };
int main() {
    int arr[10]; int &r = arr[0]; char c = 0;
    std::printf("%d %d %d %d\n", (int)sizeof(arr), (int)sizeof(arr) / (int)sizeof(*arr), (int)sizeof(r), (int)sizeof c);
    std::printf("%d %d %d %d\n", (int)sizeof(E), (int)sizeof(P), (int)sizeof(Q), (int)sizeof(Vt));
    std::printf("%d %d %d\n", (int)sizeof(void *), (int)sizeof(int (*)(int)), (int)sizeof(int E::*));
    std::printf("%d %d %d\n", (int)sizeof(effect()), (int)sizeof(c + c), (int)sizeof(1.0f * 2));
    std::printf("%d %d %d\n", (int)sizeof(Big), (int)sizeof(Big[3]), (int)sizeof(Big *));
    std::printf("%d %d %d %d\n", (int)sizeof(bool), (int)sizeof(wchar_t), (int)sizeof(char16_t), (int)sizeof(char32_t));
    std::printf("%d %d %d\n", (int)sizeof(short), (int)sizeof(long), (int)sizeof(long long));
    std::printf("%d %d %d\n", (int)sizeof("abc"), (int)sizeof(L"abc"), (int)sizeof(u8"abc"));
    std::printf("%d %d\n", (int)alignof(Q), (int)alignof(Big));
    std::printf("%d\n", (int)sizeof(E[5]));
    return 0;
}
