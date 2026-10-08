// [class.virtual]/4: only a virtual function may be marked `final`, and the
// slot search is what decides whether this is one. clang: "only virtual member
// functions can be marked 'final'".
struct S { int f() final { return 0; } };
int main() { S s; return s.f(); }
