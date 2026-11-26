// [except.handle]/13: a return in a handler of a constructor's function-try-block is ill-formed
struct S { int v; S(int x) try : v(x) { if (x < 0) throw 1; } catch (...) { return; } };
int main() { S s(1); return s.v - 1; }
