// [dcl.link]/7: a declaration written straight after extern "C" is treated as extern already,
// and "shall not specify a storage class" - static would contradict it. clang refuses it too.
extern "C" static int a;
int main() { return 0; }
