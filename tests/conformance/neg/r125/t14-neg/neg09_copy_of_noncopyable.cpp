struct NoCopy { NoCopy() {} NoCopy(const NoCopy &) = delete; };
int main() { NoCopy a; NoCopy b = a; (void)b; return 0; }
