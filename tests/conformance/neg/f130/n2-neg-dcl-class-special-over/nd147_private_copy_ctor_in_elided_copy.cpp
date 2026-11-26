class S { S(const S &); public: S() {} }; S f(); int main() { S s = f(); return 0; }
