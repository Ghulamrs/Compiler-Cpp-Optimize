// A static data member hides a namespace-scope variable of the same name in an
// unqualified lookup inside a member function - [basic.lookup.unqual]: class scope
// is searched before the namespace, whether the member is static or not.
extern "C" int printf(const char *, ...);

int count = 100;
double scale = 0.5;
int limit = 7;
int table[3] = { 9, 9, 9 };

struct S {
    static int count;
    static double scale;
    static const int limit = 3;
    static int table[3];
    int get() { return count; }
    void bump() { count += 1; }
    static int sget() { return count + limit; }
    double scaled(int x) const { return x * scale; }
    int at(int i) { return table[i]; }
    int sum();
};
int S::count = 1;
double S::scale = 2.0;
int S::table[3] = { 1, 2, 3 };

int S::sum() { return count + limit + table[2]; }

struct D : S {
    int dget() { return count * 10 + limit; }
};

namespace n {
    int value = 50;
    struct T {
        static int value;
        int get() { return value; }
    };
    int T::value = 5;
}

int main() {
    S s;
    s.bump();
    printf("%d %d %d\n", s.get(), S::sget(), count);
    printf("%g %d %d\n", s.scaled(3), s.at(1), s.sum());
    D d;
    printf("%d %d\n", d.dget(), limit);
    n::T t;
    printf("%d %d\n", t.get(), n::value);
    printf("%g %d\n", scale, table[0]);
    return 0;
}
