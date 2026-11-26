struct S { int a; }; int main() { volatile S vs; vs.a = 1; S s = vs; return s.a; }
