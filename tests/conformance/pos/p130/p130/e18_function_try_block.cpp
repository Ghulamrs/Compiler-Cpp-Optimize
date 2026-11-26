struct S { int v; S(int x) try : v(x) { if (x < 0) throw 1; } catch (...) {} };
int main() { try { S s(-1); } catch (int) { return 0; } return 1; }
