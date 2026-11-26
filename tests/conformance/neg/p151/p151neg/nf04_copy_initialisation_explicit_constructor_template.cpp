// [over.match.copy]/1: copy-initialisation from an int takes no explicit constructor, template or not.
struct W { template <class U> explicit W(U) {} };
int main() { W w = 1; (void)w; return 0; }
