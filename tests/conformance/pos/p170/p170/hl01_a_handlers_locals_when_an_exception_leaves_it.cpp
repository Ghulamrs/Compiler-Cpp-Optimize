// [except.ctor]/1: as control passes from a throw to a handler, every automatic object constructed since the try
// block was entered is destroyed - the locals of a handler the exception leaves among them, innermost first,
// whatever blocks and try blocks of the handler they stand in. Until 1.69 a cleanup inside a handler handed over
// to the handler's own end and the handler's other locals stood (the Itanium targets and the C6000; eh05, eh09).
#include <cstdio>

struct T { int i; T(int i) : i(i) {} ~T() { std::printf("~%d ", i); } };
void thrower(int x) { throw x; }

// blocks inside a handler, three deep
void blocks() {
    try { thrower(1); }
    catch (int e) { T a(e); { T b(e + 1); { T c(e + 2); thrower(e + 10); } } }
}

// a try inside a handler whose own handler throws: both handlers' locals go
void nested() {
    T outer(20);
    try { thrower(2); }
    catch (int) {
        T b(21);
        { T c(22);
          try { thrower(3); }
          catch (int) { T d(23); { T e(24); throw 3.5; } } }
    }
}

// a try inside a handler that catches nothing of what is thrown: the handler's locals before it go
void unmatched() {
    try { thrower(4); }
    catch (int) {
        T b(31);
        { T c(32);
          try { T d(33); throw 4.5; }
          catch (char) { std::printf("not here "); } }
    }
}

// the handler's by-value parameter has a destructor of its own
struct E { int k; E(int k) : k(k) {} E(const E &o) : k(o.k) {} ~E() { std::printf("~E%d ", k); } };
void byValue() {
    try { throw E(5); }
    catch (E e) { T a(41); { T b(42); thrower(e.k + 100); } }
}

// and a loop in a handler, left by an exception from its body
void loop() {
    try { thrower(6); }
    catch (int e) { T a(51); for (int i = 0; i < 3; i++) { T b(52 + i); if (i == 1) thrower(e + 200); } }
}

int main() {
    try { blocks(); } catch (int e) { std::printf("caught %d\n", e); }
    try { nested(); } catch (double d) { std::printf("caught %g\n", d); }
    try { unmatched(); } catch (double d) { std::printf("caught %g\n", d); }
    try { byValue(); } catch (int e) { std::printf("caught %d\n", e); }
    try { loop(); } catch (int e) { std::printf("caught %d\n", e); }
    return 0;
}
