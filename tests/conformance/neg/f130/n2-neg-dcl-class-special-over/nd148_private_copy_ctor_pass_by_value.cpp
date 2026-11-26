class S { S(const S &); public: S() {} }; void g(S); int main() { S s; g(s); return 0; }
