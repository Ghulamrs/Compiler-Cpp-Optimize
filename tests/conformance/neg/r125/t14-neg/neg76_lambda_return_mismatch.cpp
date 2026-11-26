int main() { auto l = [](bool b) { if (b) return 1; else return 2.0; }; return (int)l(true); }
