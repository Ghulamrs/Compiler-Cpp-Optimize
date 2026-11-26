// Every place that may name a class's private or protected typedef or alias, [class.access]/1-2, [class.access.nest]/1,
// [class.friend]/2, and a member defined outside its class, [class.access]/6.
#include <cstdio>

class S {
    typedef int T;
    using U = long;
    friend int peek();
    friend class F;
public:
    T get() const;
    U twice(T v) const {
        S::U r = v;
        return r * 2;
    }
    struct Inner {
        int f() const {
            S::T x = 7;
            return x;
        }
    };
    int viaLambda() const {
        auto l = [](int v) { S::T y = v + 1; return y; };
        return l(4);
    }
};

S::T S::get() const {
    S::T x = 5;
    return x;
}

int peek() {
    S::T x = 11;
    return x;
}

class F {
public:
    long g() const {
        S::U u = 13;
        return u;
    }
};

class B {
protected:
    typedef int P;
private:
    typedef int Q;
public:
    typedef int R;
};

struct D : B {
    int h() const {
        B::P a = 17;
        D::P b = 19;
        return a + b;
    }
};

int main() {
    S s;
    S::Inner in;
    F f;
    D d;
    B::R r = 23;
    D::R r2 = 29;
    std::printf("%d %ld %d %d %d %ld %d %d %d\n", s.get(), s.twice(3), in.f(), s.viaLambda(), peek(), f.g(),
                d.h(), r, r2);
    return 0;
}
