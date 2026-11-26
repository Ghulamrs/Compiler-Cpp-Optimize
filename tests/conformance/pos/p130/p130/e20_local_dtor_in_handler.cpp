struct G { ~G() {} };
int main() { try { throw 1; } catch (int) { G g; return 0; } return 1; }
