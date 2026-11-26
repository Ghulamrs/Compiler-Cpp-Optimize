int main() { auto l = [](bool b) { if (b) return; return 1; }; l(true); return 0; }
