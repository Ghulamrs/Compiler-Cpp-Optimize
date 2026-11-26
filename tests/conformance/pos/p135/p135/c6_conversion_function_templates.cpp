// R6: conversion function templates, T deduced from the type required ([temp.deduct.conv]).
#include <cstdio>
struct S { int v; operator int() const { return v; } template <class T> operator T *() const { return nullptr; } };
struct Any { template <class T> operator T() const { return T(7); } };
struct Base { template <class T> operator const T &() const { static T t = T(4); return t; } };
struct Derived : Base {};
int main() {
    S s{3};
    int i = s;
    double *dp = s;
    const char *cp = s;
    Any a;
    long l = a;
    double d = a;
    Derived x;
    const int &r = x;
    std::printf("%d %d %d %ld %g %d\n", i, (int)(dp == nullptr), (int)(cp == nullptr), l, d, r);
    return 0;
}