// `char s[3] = "abc";`, refused: [dcl.init.string]/2 wants room for the '\0'.
// C allows exactly this and drops the terminator; C++ does not, and the array
// would hold no string at all for strlen or printf to stop at.
int main(void) {
    char s[3] = "abc";
    return s[0];
}
