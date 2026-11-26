// [cpp.predefined]/2: __STDCPP_THREADS__ is defined, as 1, if and only if a program can have more than one
// thread of execution. A hosted target starts threads and defines it; the freestanding C6747 runs one and does
// not. Until 1.68 no target defined it.
#include <cstdio>

#if __STDC_HOSTED__
#  if !defined(__STDCPP_THREADS__)
#    error "a hosted target, where <thread> starts a thread, and __STDCPP_THREADS__ is not defined"
#  elif __STDCPP_THREADS__ != 1
#    error "__STDCPP_THREADS__ is defined and is not 1"
#  endif
#else
#  if defined(__STDCPP_THREADS__)
#    error "a freestanding target with one thread, and __STDCPP_THREADS__ is defined"
#  endif
#endif

#if __STDC_HOSTED__
#include <thread>
static int ran = 0;
static void work() { ran = 1; }
static int check() { std::thread t(work); t.join(); return ran; }
#else
static int check() { return 1; }
#endif

int main() {
    std::printf("%d\n", check());
    return 0;
}
