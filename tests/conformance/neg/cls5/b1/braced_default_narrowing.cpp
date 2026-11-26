struct P { char c; };
int f(P p = {300}) { return p.c; }
int main() { return f(); }
