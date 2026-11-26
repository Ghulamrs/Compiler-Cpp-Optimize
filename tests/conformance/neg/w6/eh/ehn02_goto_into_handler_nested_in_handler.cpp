// [except.handle]/2: a goto may not transfer control into a handler, here one inside another handler
int main() {
    try { throw 1; }
    catch (int) {
        goto in;
        try { throw 2; }
        catch (int) { in: return 0; }
    }
    return 1;
}
