int main() { auto l = [](bool b) { if (b) return 1; return 2.5; }; return (int)l(true); }
