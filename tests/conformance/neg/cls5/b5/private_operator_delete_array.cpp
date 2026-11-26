class S { static void operator delete[](void *); public: S() {} ~S() {} };
int main() { S *p = new S[2]; delete[] p; return 0; }
