// C++11's own two attributes are read - `[[noreturn]]`, `[[carries_dependency]]`
// - and change nothing; `[[deprecated]]` is C++14 and is refused with the
// version number, as any other attribute is.
[[deprecated]] void die();
int main() { return 0; }
