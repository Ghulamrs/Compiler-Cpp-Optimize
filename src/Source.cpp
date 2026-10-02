#include "Source.h"

#include <cstdio>
#include <cstdlib>

Source::Source(std::string name, std::string text)
    : name_(std::move(name)), text_(std::move(text)) {
    if (text_.empty() || text_.back() != '\n') text_.push_back('\n');
    files_.push_back(name_);
}

Source::Source(std::string name, std::string text, std::vector<std::string> files,
               std::vector<Line> lines)
    : name_(std::move(name)), text_(std::move(text)),
      files_(std::move(files)), lines_(std::move(lines)) {
    if (text_.empty() || text_.back() != '\n') text_.push_back('\n');

    if (files_.empty()) files_.push_back(name_);
}

Source Source::fromFile(const std::string &path) {
    std::FILE *fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        std::fprintf(stderr, "cannot open %s\n", path.c_str());
        std::exit(1);
    }
    std::string buf;
    char chunk[4096];
    std::size_t n;
    while ((n = std::fread(chunk, 1, sizeof chunk, fp)) > 0) buf.append(chunk, n);
    std::fclose(fp);
    return Source(path, std::move(buf));
}

void Source::indexLines() const {
    lineStarts_.push_back(0);
    for (std::size_t i = 0; i < text_.size(); i++)
        if (text_[i] == '\n' && i + 1 < text_.size()) lineStarts_.push_back(i + 1);
}

void Source::lineAt(std::size_t pos, int *line, std::size_t *start) const {
    if (lineStarts_.empty()) indexLines();

    std::size_t lo = 0, hi = lineStarts_.size() - 1;
    while (lo < hi) {
        std::size_t mid = lo + (hi - lo + 1) / 2;
        if (lineStarts_[mid] <= pos) lo = mid; else hi = mid - 1;
    }
    *line = static_cast<int>(lo) + 1;
    *start = lineStarts_[lo];
}

int Source::packAt(std::size_t pos) const {
    if (packs_.empty()) return 0;
    int line;
    std::size_t start;
    lineAt(pos, &line, &start);
    int value = 0;
    for (std::size_t i = 0; i < packs_.size(); i++)
        if (packs_[i].line < static_cast<std::size_t>(line)) value = packs_[i].value;
    return value;
}

Source::Place Source::locate(std::size_t pos) const {
    if (pos > text_.size()) pos = text_.size();

    int lineNo = 1;
    std::size_t lineStart = 0;
    lineAt(pos, &lineNo, &lineStart);

    Place at;
    at.file = 0;
    at.line = lineNo;
    at.column = static_cast<int>(pos - lineStart) + 1;

    if (!lines_.empty() && static_cast<std::size_t>(lineNo) <= lines_.size()) {
        const Line &l = lines_[static_cast<std::size_t>(lineNo) - 1];
        if (l.file >= 0 && static_cast<std::size_t>(l.file) < files_.size())
            at.file = l.file;
        at.line = l.line;
    }
    return at;
}

std::string Source::where(std::size_t pos) const {
    if (pos > text_.size()) pos = text_.size();
    const Place at = locate(pos);
    return files_[static_cast<std::size_t>(at.file)] + ":" + std::to_string(at.line) + ":" +
           std::to_string(at.column);
}

void Source::fail(std::size_t pos, const std::string &message) const {
    // Inside a trial nothing is printed and nothing exits.
    if (trials_ > 0) {
        // **The same rule tools/exclusions derives the exclusion list from**, bare "yet" included -
        // which is what catches the mangler's "no Itanium linkage name yet", a limitation that
        // reads nothing like the parser's refusals and is just as much a fact about the compiler.
        const bool unsupported =
            message.find("not supported") != std::string::npos ||
            message.find("not implemented") != std::string::npos ||
            message.find("is C++1") != std::string::npos ||
            message.find(" yet") != std::string::npos;
        throw SubstitutionFailure{ message, unsupported, pos };
    }

    if (pos > text_.size()) pos = text_.size();

    int lineNo = 1;
    std::size_t lineStart = 0;
    lineAt(pos, &lineNo, &lineStart);

    std::size_t lineEnd = text_.find('\n', pos);
    if (lineEnd == std::string::npos) lineEnd = text_.size();

    Place at = locate(pos);
    const std::string &file = files_[static_cast<std::size_t>(at.file)];

    std::string text = file + ":" + std::to_string(at.line) + ":" +
                       std::to_string(at.column) + ": error: " +
                       message + "\n";

    text += "    ";
    text.append(text_, lineStart, lineEnd - lineStart);
    text += "\n    ";

    for (std::size_t i = lineStart; i < pos; i++)
        text += (text_[i] == '\t') ? '\t' : ' ';
    text += "^\n";

    std::fwrite(text.data(), 1, text.size(), stderr);
    std::exit(1);
}
