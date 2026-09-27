// #ifdef takes one name: anything but a comment after it is refused, where clang warns
#ifdef KNOWN extra
#endif
int main() { return 0; }
