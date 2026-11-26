class S { ~S() {} public: S() {} };
int main() { static S s; return 0; }
