// [except.handle]/2: a goto from a handler into a try block written inside it
int main() {
    try { throw 1; }
    catch (int) {
        goto in;
        try { in: throw 2; }
        catch (int) { return 0; }
    }
    return 1;
}
