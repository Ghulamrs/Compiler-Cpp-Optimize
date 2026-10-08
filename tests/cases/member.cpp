// Member functions and `this` - the second step of rung 3, and the step where
// "inside the class" starts to exist.
//
// A member function is a free function with one extra leading pointer, which
// is why the backends needed nothing: `this` is parameter zero and every
// backend already knew how to pass a pointer. What is new is the name - both
// ABIs spell the class into it, and the Microsoft one spells the access in as
// well, so a private member has a different symbol there and the same symbol
// on Linux. Every one of those was measured against clang.
//
// Uses::viaPrivate is the point of the whole step: a private member is
// reachable from another member of the same class and from nowhere else.
extern "C" { int printf(const char *, ...); }

class Counter {
public:
    int start(int n);
    int bump();
    int bump(int by);
    int value() const;
    int doubled() const;
private:
    int count;
    int hidden();
};

int Counter::start(int n) { count = n; return count; }
int Counter::bump() { count = count + 1; return count; }
int Counter::bump(int by) { count = count + by; return count; }
int Counter::value() const { return count; }
int Counter::hidden() { return count * 10; }
int Counter::doubled() const { return this->count * 2; }

class Uses {
public:
    int viaPrivate();
private:
    int secret();
    int n;
};
int Uses::secret() { return 7; }
int Uses::viaPrivate() { n = secret(); return n; }

int main(void) {
    Counter c;
    Counter *p = &c;
    Uses u;
    int s = c.start(10);
    int b1 = c.bump();
    int b2 = c.bump(5);
    printf("%d %d %d\n", s, b1, b2);
    printf("%d %d\n", c.value(), c.doubled());
    int b3 = p->bump();
    printf("%d %d\n", b3, p->value());
    printf("%d\n", u.viaPrivate());
    return 0;
}
