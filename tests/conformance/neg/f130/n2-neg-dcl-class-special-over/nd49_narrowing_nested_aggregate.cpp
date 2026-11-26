struct In { char c; }; struct Out { In i; }; int main() { int v = 1; Out o{{v}}; return o.i.c; }
