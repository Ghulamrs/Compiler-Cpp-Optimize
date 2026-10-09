// `[[deprecated]]` is C++14 and is refused with the version number; an attribute this compiler
// does not know is ignored, as [dcl.attr.grammar]/5 says (attribute-ignored). clang:
// "use of the 'deprecated' attribute is a C++14 extension" (4:3).
[[deprecated]] void die();
int main() { return 0; }
