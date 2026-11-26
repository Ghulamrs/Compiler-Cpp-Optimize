// [dcl.link]/6: at most one function of a name has C linkage.
extern "C" int h(int);
extern "C" int h(long);
int main() { return 0; }
