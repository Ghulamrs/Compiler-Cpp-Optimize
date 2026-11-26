#include <iostream>
#include <sstream>
#include <iterator>
#include <vector>
#include <algorithm>
#include <string>

int main() {
    std::istringstream nums("3 1 4 1 5 9");
    std::vector<int> v((std::istream_iterator<int>(nums)), std::istream_iterator<int>());
    std::sort(v.begin(), v.end());
    std::copy(v.begin(), v.end(), std::ostream_iterator<int>(std::cout, ","));
    std::cout << '\n';

    std::istringstream text("raw  text\n");
    std::string s((std::istreambuf_iterator<char>(text)), std::istreambuf_iterator<char>());
    std::cout << '[' << s.substr(0, s.size() - 1) << "] " << s.size() << '\n';

    std::ostringstream out;
    std::ostreambuf_iterator<char> it(out);
    const char word[] = "buffered";
    for (const char *p = word; *p; ++p) *it++ = *p;
    std::cout << out.str() << ' ' << it.failed() << '\n';

    std::istringstream words("one two three");
    std::istream_iterator<std::string> w(words), end;
    std::cout << std::distance(w, end) << " words\n";
    return 0;
}
