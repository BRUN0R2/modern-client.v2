// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

// The conformance suite's runner (conformance/README.md). For every case: the tree is lossless, the
// diagnostics are exactly those its comments expect, and, where it has one, the tree is the one written
// beside it. For the whole suite: every diagnostic is reached by a case, and the reserved words of the
// grammar are the compiler's.

// stderr is a macro, which `import std;` does not export.
#include <cstdio>

import std;
import rook.compiler;

namespace {

namespace compiler = rook::compiler;

constexpr std::string_view kSourceExtension = ".rook";
constexpr std::string_view kTreeExtension = ".tree";
constexpr std::string_view kAcceptsDirectory = "accepts";
constexpr std::string_view kRefusesDirectory = "refuses";
constexpr std::string_view kUpdateFlag = "--update-trees";
constexpr std::string_view kSameLineMark = "//~ error[";
constexpr std::string_view kNextLineMark = "//~v error[";
constexpr std::string_view kMarkEnd = "]";
constexpr int kSuccess = 0;
constexpr int kFailure = 1;
constexpr std::size_t kExpectedArguments = 3;

/// One diagnostic where a case expects it, or where the compiler reported it.
struct Finding {
    std::uint32_t line = 0;
    std::string code;

    auto operator<=>(const Finding&) const = default;
};

/// The failures of a run, printed at the end of each case.
class Failures {
public:
    void add(std::string_view source, std::string_view what) {
        count += 1;
        std::println(stderr, "FAIL {}: {}", source, what);
    }

    [[nodiscard]] auto total() const noexcept -> std::size_t { return count; }

private:
    std::size_t count = 0;
};

[[nodiscard]] auto readFile(const std::filesystem::path& path) -> std::optional<std::string> {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::nullopt;
    }
    std::string bytes{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    return bytes;
}

/// What the comments of a case expect: `//~ error[R0030]` on the line it is written on, `//~v error[R0030]`
/// on the next one, for a line that swallows comments, such as an unterminated string's.
[[nodiscard]] auto expectedFindings(const compiler::SyntaxTree& tree, const compiler::LineMap& lines)
    -> std::vector<Finding> {
    std::vector<Finding> expected;
    for (const compiler::Token& token : tree.tokens()) {
        if (token.kind != compiler::TokenKind::LineComment) {
            continue;
        }
        const std::string_view text = tree.text(token.range);
        const bool isNextLine = text.starts_with(kNextLineMark);
        if (!isNextLine && !text.starts_with(kSameLineMark)) {
            continue;
        }
        const std::uint32_t line = lines.positionOf(token.range.start).line + (isNextLine ? 1U : 0U);
        const std::string_view mark = isNextLine ? kNextLineMark : kSameLineMark;
        std::string_view rest = text.substr(mark.size());
        while (true) {
            const std::size_t end = rest.find(kMarkEnd);
            if (end == std::string_view::npos) {
                break;
            }
            expected.push_back(Finding{.line = line, .code = std::string{rest.substr(0, end)}});
            const std::size_t next = rest.find('[', end);
            if (next == std::string_view::npos) {
                break;
            }
            rest = rest.substr(next + 1);
        }
    }
    return expected;
}

[[nodiscard]] auto foundFindings(const compiler::ParseResult& result, const compiler::LineMap& lines)
    -> std::vector<Finding> {
    std::vector<Finding> found;
    found.reserve(result.diagnostics.size());
    for (const compiler::Diagnostic& diagnostic : result.diagnostics) {
        found.push_back(Finding{.line = lines.positionOf(diagnostic.range.start).line,
                                .code = compiler::diagnosticCodeText(diagnostic.code)});
    }
    return found;
}

[[nodiscard]] auto describeFindings(const std::vector<Finding>& findings) -> std::string {
    std::string text;
    for (const Finding& finding : findings) {
        text += std::format(" {}@{}", finding.code, finding.line);
    }
    return text.empty() ? " none" : text;
}

/// The tree's tokens, in order, must be the source itself, and its nodes must hold them in that order.
void checkLossless(const compiler::SyntaxTree& tree, std::string_view source, Failures& failures,
                   std::string_view name) {
    std::string written;
    written.reserve(source.size());
    std::vector<std::span<const compiler::SyntaxElement>> pending{tree.children(tree.root())};
    while (!pending.empty()) {
        auto& children = pending.back();
        if (children.empty()) {
            pending.pop_back();
            continue;
        }
        const compiler::SyntaxElement element = children.front();
        children = children.subspan(1);
        if (element.isToken()) {
            written += tree.text(tree.token(element.token()).range);
        } else {
            pending.push_back(tree.children(element.node()));
        }
    }
    if (written != source) {
        failures.add(name, "the tree does not write the source back");
    }
}

/// Every diagnostic must render with its code, a snippet and a fix.
void checkRendering(const compiler::ParseResult& result, const compiler::LineMap& lines, Failures& failures,
                    std::string_view name) {
    for (const compiler::Diagnostic& diagnostic : result.diagnostics) {
        const std::string text = compiler::renderDiagnostic(diagnostic, name, result.tree.source(), lines);
        const std::string header = std::format("error[{}]", compiler::diagnosticCodeText(diagnostic.code));
        if (!text.starts_with(header) || !text.contains('^') || !text.contains("= fix: ") ||
            diagnostic.fix.empty() || diagnostic.message.empty()) {
            failures.add(name, std::format("a diagnostic renders incompletely:\n{}", text));
        }
    }
}

/// Compares the tree with the file beside its source, or rewrites that file. To add a tree to a case,
/// create the empty file and run with --update-trees.
void checkTree(const compiler::SyntaxTree& tree, const std::filesystem::path& treePath, bool update,
               Failures& failures, const std::string& name) {
    const std::string printed = compiler::printSyntaxTree(tree);
    if (update) {
        std::ofstream file{treePath, std::ios::binary | std::ios::trunc};
        file << printed;
        return;
    }
    const std::optional<std::string> expected = readFile(treePath);
    if (!expected) {
        failures.add(name, "its .tree file cannot be read");
    } else if (*expected != printed) {
        failures.add(name, std::format("its tree differs from its .tree file; it is:\n{}", printed));
    }
}

/// Runs one case, adding the codes it reached to `reached`.
void runCase(const std::filesystem::path& source, std::string_view directory, bool update, Failures& failures,
             std::set<std::string>& reached) {
    const std::string name = source.generic_string();
    const std::optional<std::string> bytes = readFile(source);
    if (!bytes) {
        failures.add(name, "cannot be read");
        return;
    }
    const compiler::ParseResult result = compiler::parse(*bytes);
    const compiler::LineMap lines{result.tree.source()};
    checkLossless(result.tree, *bytes, failures, name);
    checkRendering(result, lines, failures, name);

    std::vector<Finding> expected = expectedFindings(result.tree, lines);
    std::vector<Finding> found = foundFindings(result, lines);
    std::ranges::sort(expected);
    std::ranges::sort(found);
    if (expected != found) {
        failures.add(name,
                     std::format("expected{}, found{}", describeFindings(expected), describeFindings(found)));
    }
    for (const Finding& finding : found) {
        reached.insert(finding.code);
    }

    if (directory == kAcceptsDirectory && !expected.empty()) {
        failures.add(name, "a source that must parse expects diagnostics");
    }
    if (directory == kRefusesDirectory && expected.empty()) {
        failures.add(name, "a source that must not parse expects no diagnostic");
    }
    std::filesystem::path treePath = source;
    treePath.replace_extension(kTreeExtension);
    if (std::filesystem::exists(treePath)) {
        checkTree(result.tree, treePath, update, failures, name);
    }
}

/// The sources of a directory of the suite, sorted.
[[nodiscard]] auto sourcesOf(const std::filesystem::path& directory) -> std::vector<std::filesystem::path> {
    std::vector<std::filesystem::path> sources;
    for (const auto& entry : std::filesystem::recursive_directory_iterator{directory}) {
        if (entry.is_regular_file() && entry.path().extension() == kSourceExtension) {
            sources.push_back(entry.path());
        }
    }
    std::ranges::sort(sources);
    return sources;
}

/// A source over the limit cannot be a file of the repository, so the runner makes one.
void checkSourceTooLarge(Failures& failures, std::set<std::string>& reached) {
    const std::string source(compiler::kMaximumSourceBytes + 1, ' ');
    const compiler::ParseResult result = compiler::parse(source);
    const std::string expected = compiler::diagnosticCodeText(compiler::DiagnosticCode::SourceTooLarge);
    if (result.diagnostics.size() != 1 ||
        compiler::diagnosticCodeText(result.diagnostics.front().code) != expected) {
        failures.add("(a source over the limit)", "expected one diagnostic saying so");
    }
    reached.insert(expected);
}

/// Neither is the source of too many errors worth a file of the repository.
void checkTooManyDiagnostics(Failures& failures, std::set<std::string>& reached) {
    const std::string source(compiler::kMaximumDiagnostics * 2, '$');
    const compiler::ParseResult result = compiler::parse(source);
    const std::string expected = compiler::diagnosticCodeText(compiler::DiagnosticCode::TooManyDiagnostics);
    if (result.diagnostics.size() != compiler::kMaximumDiagnostics + 1 ||
        compiler::diagnosticCodeText(result.diagnostics.back().code) != expected) {
        failures.add("(a source of too many errors)", "expected the first ones and one saying so");
    }
    reached.insert(expected);
}

void checkEveryCodeIsReached(const std::set<std::string>& reached, Failures& failures) {
    for (const compiler::DiagnosticCode code : compiler::kDiagnosticCodes) {
        const std::string text = compiler::diagnosticCodeText(code);
        if (!reached.contains(text)) {
            failures.add("the suite", std::format("no case reaches {}", text));
        }
    }
    if (reached.size() != compiler::kDiagnosticCodes.size()) {
        failures.add("the suite", "a case reaches a code that is not in kDiagnosticCodes");
    }
}

/// The words the rule `name` of the grammar lists between quotes.
[[nodiscard]] auto wordsOfRule(std::string_view grammar, std::string_view name) -> std::set<std::string> {
    std::set<std::string> words;
    const std::size_t start = grammar.find(std::format("\n{} ", name));
    if (start == std::string_view::npos) {
        return words;
    }
    const std::size_t end = grammar.find(" ;", start);
    std::string_view rule = grammar.substr(start, end - start);
    for (std::size_t quote = rule.find('"'); quote != std::string_view::npos;) {
        const std::size_t close = rule.find('"', quote + 1);
        words.insert(std::string{rule.substr(quote + 1, close - quote - 1)});
        quote = rule.find('"', close + 1);
    }
    return words;
}

/// The reserved words of the grammar and the compiler must be the same, and each must be a token.
void checkGrammar(const std::filesystem::path& path, Failures& failures) {
    const std::string name = path.generic_string();
    const std::optional<std::string> grammar = readFile(path);
    if (!grammar) {
        failures.add(name, "cannot be read");
        return;
    }
    const std::set<std::string> words = wordsOfRule(*grammar, "reservedWord");
    for (const compiler::ReservedWord& word : compiler::kReservedWords) {
        if (!words.contains(std::string{word.spelling})) {
            failures.add(
                name, std::format("`{}` is reserved in the compiler and not in the grammar", word.spelling));
        }
    }
    for (const std::string& word : words) {
        if (!compiler::reservedWordKind(word)) {
            failures.add(name, std::format("`{}` is reserved in the grammar and not in the compiler", word));
        }
    }
    for (const std::string& word : wordsOfRule(*grammar, "contextualWord")) {
        if (compiler::reservedWordKind(word)) {
            failures.add(name,
                         std::format("`{}` is contextual in the grammar and reserved in the compiler", word));
        }
    }
}

} // namespace

auto main(int argumentCount, char** arguments) -> int {
    const auto all = std::span{arguments, static_cast<std::size_t>(argumentCount)};
    if (all.size() < kExpectedArguments) {
        std::println(stderr, "usage: rookConformance <conformance directory> <grammar file> [{}]",
                     kUpdateFlag);
        return kFailure;
    }
    const std::filesystem::path suite{all[1]};
    const std::filesystem::path grammar{all[2]};
    const bool update =
        all.size() > kExpectedArguments && std::string_view{all[kExpectedArguments]} == kUpdateFlag;

    Failures failures;
    std::set<std::string> reached;
    std::size_t cases = 0;
    for (const std::string_view directory : {kAcceptsDirectory, kRefusesDirectory}) {
        for (const std::filesystem::path& source : sourcesOf(suite / directory)) {
            runCase(source, directory, update, failures, reached);
            cases += 1;
        }
    }
    if (cases == 0) {
        failures.add("the suite", "it has no cases");
    }
    checkSourceTooLarge(failures, reached);
    checkTooManyDiagnostics(failures, reached);
    checkEveryCodeIsReached(reached, failures);
    checkGrammar(grammar, failures);

    std::println("{} cases, {} failures", cases, failures.total());
    return failures.total() == 0 ? kSuccess : kFailure;
}
