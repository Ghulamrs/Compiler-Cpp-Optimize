// va_arg on a va_list the function was handed rather than started - C 7.15.1 asks only va_start to
// be in a function declared with '...', and vprintf and every v-function is this shape. cpp11 refused
// it until 2026-10-06; RTS6x's printf formatter, a class reading the arguments, is what found it.
#include <stdarg.h>
extern "C" int printf(const char *, ...);

static long long total(int n, va_list *args)
{
    long long sum = 0;
    for (int i = 0; i < n; i++) {
        if (i % 2 == 0) sum += va_arg(*args, int);
        else sum += (long long)va_arg(*args, double);
    }
    return sum;
}

// The vprintf shape: the list by value, read where it arrives.
static long long byValue(int n, va_list args)
{
    long long sum = 0;
    for (int i = 0; i < n; i++) sum += (i % 2 == 0) ? va_arg(args, int) : (long long)va_arg(args, double);
    return sum;
}

static long long gather(int n, ...)
{
    va_list args;
    va_start(args, n);
    long long first = total(2, &args);
    long long rest = byValue(n - 2, args);
    va_end(args);
    return first * 1000 + rest;
}

int main()
{
    printf("%lld\n", gather(5, 3, 4.5, 10, 20.25, 7));
    printf("%lld\n", gather(2, -1, 100.0));
    return 0;
}
