#include <regex>

// [re.synopt]: syntax_option_type is a bitmask type of its own; an int does not convert to it.
int main() {
    std::regex_constants::syntax_option_type f = 1;
    return f != 0 ? 0 : 1;
}
