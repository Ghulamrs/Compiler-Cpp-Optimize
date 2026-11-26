// [dcl.link]/5: declared with C++ linkage first, so a later extern "C" changes it.
int g(int);
extern "C" int g(int);
int main() { return 0; }
