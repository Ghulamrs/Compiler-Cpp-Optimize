class S { ~S() {} public: S() {} }; int main() { S *p = new S; delete p; return 0; }
