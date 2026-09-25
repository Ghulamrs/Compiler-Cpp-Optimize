// A frame slot is only as aligned as the stack it hangs from - 16 on the
// hosts, 8 on the C6000 - and past that the frame would need realigning.
int main() {
    alignas(32) char buf[3];
    buf[0] = 1;
    return buf[0];
}
