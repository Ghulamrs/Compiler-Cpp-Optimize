// [expr.new]/2: `new auto` needs its initialiser.
int main() { auto *p = new auto; return 0; }