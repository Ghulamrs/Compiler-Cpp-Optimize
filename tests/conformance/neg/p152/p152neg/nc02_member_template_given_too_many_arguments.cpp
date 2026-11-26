struct S {
    template <class T> int f(T t) { return (int)t; }
};
int main() {
    S s;
    return s.f<int, int>(1);
}
