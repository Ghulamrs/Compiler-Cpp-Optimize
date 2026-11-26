// [temp]/4: a template has no C linkage.
extern "C" { template <class T> struct Box { T v; }; }
int main() { return 0; }