#include <regex>

// [re.regex.construct]: basic_regex's constructor from a C string is explicit, so copy-initialisation cannot use it.
int main() {
    std::regex r = "abc";
    return r.mark_count() == 0 ? 0 : 1;
}
