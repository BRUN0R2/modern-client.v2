// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

module rook.compiler;

import std;

namespace rook::compiler {

namespace {

/// The name of each kind of node, in the order of the enum.
constexpr std::array<std::string_view, static_cast<std::size_t>(SyntaxKind::Count)> kKindNames{
    "SourceFile",
    "ImportDeclaration",
    "ImportList",
    "PluginDeclaration",
    "Attribute",
    "FunctionDeclaration",
    "ForwardDeclaration",
    "ParameterList",
    "Parameter",
    "GenericParameterList",
    "GenericParameter",
    "StructDeclaration",
    "ComponentDeclaration",
    "FieldList",
    "Field",
    "EnumDeclaration",
    "Variant",
    "VariantField",
    "ClassDeclaration",
    "InterfaceDeclaration",
    "ImplementationDeclaration",
    "MemberList",
    "MemberVariable",
    "NewtypeDeclaration",
    "SuffixClause",
    "DimensionClause",
    "RangeClause",
    "DeriveClause",
    "HandleDeclaration",
    "VarsDeclaration",
    "VarsField",
    "ConstantDeclaration",
    "GlobalDeclaration",
    "TypeAlias",
    "PathType",
    "GenericArgumentList",
    "TupleType",
    "ArrayType",
    "FunctionType",
    "OptionalType",
    "Block",
    "LetStatement",
    "VarStatement",
    "AssignmentStatement",
    "ExpressionStatement",
    "WhileStatement",
    "ForStatement",
    "LoopStatement",
    "DeferStatement",
    "ReturnStatement",
    "BreakStatement",
    "ContinueStatement",
    "LiteralExpression",
    "StringExpression",
    "Interpolation",
    "NameExpression",
    "ImplicitMemberExpression",
    "MemberExpression",
    "CallExpression",
    "ArgumentList",
    "Argument",
    "IndexExpression",
    "TryExpression",
    "PrefixExpression",
    "AwaitExpression",
    "BinaryExpression",
    "RangeExpression",
    "IsExpression",
    "ParenthesizedExpression",
    "TupleExpression",
    "ArrayExpression",
    "ClosureExpression",
    "ClosureParameterList",
    "StructLiteral",
    "FieldInitializerList",
    "FieldInitializer",
    "IfExpression",
    "LetCondition",
    "MatchExpression",
    "MatchArm",
    "MatchGuard",
    "WildcardPattern",
    "LiteralPattern",
    "WrapperPattern",
    "NamePattern",
    "MemberPattern",
    "TuplePattern",
    "OrPattern",
    "PatternFieldList",
    "PatternField",
    "Error",
};

constexpr std::size_t kIndentation = 2;

/// Whether printSyntaxTree shows a token: a tree's shape does not depend on where the spaces are.
[[nodiscard]] constexpr auto isPrinted(TokenKind kind) noexcept -> bool {
    return !isTrivia(kind) || kind == TokenKind::DocumentationComment || kind == TokenKind::Invalid;
}

/// Appends a token's text, escaped where it could not be read on one line.
void appendTokenText(std::string& output, std::string_view text) {
    for (const char character : text) {
        if (character == '\n') {
            output += "\\n";
        } else if (character == '\r') {
            output += "\\r";
        } else if (character == '\t') {
            output += "\\t";
        } else {
            output += character;
        }
    }
}

/// A node whose children are still being printed: the children left.
struct PrintFrame {
    std::span<const SyntaxElement> children;
    std::size_t depth;
};

/// Writes a node's line: its name and the tokens directly under it.
void printNodeLine(std::string& output, const SyntaxTree& tree, NodeId id, std::size_t depth) {
    output.append(depth * kIndentation, ' ');
    output += syntaxKindName(tree.kind(id));
    for (const SyntaxElement child : tree.children(id)) {
        if (!child.isToken()) {
            continue;
        }
        const Token& token = tree.token(child.token());
        if (isPrinted(token.kind) && token.kind != TokenKind::EndOfFile) {
            output += " `";
            appendTokenText(output, textForTerminal(tree.text(token.range)));
            output += '`';
        }
    }
    output += '\n';
}

} // namespace

auto syntaxKindName(SyntaxKind kind) noexcept -> std::string_view {
    return kKindNames[static_cast<std::size_t>(kind)];
}

auto printSyntaxTree(const SyntaxTree& tree) -> std::string {
    std::string output;
    printNodeLine(output, tree, tree.root(), 0);
    std::vector<PrintFrame> frames{PrintFrame{.children = tree.children(tree.root()), .depth = 1}};
    while (!frames.empty()) {
        PrintFrame& frame = frames.back();
        if (frame.children.empty()) {
            frames.pop_back();
            continue;
        }
        const SyntaxElement child = frame.children.front();
        frame.children = frame.children.subspan(1);
        if (!child.isToken()) {
            const std::size_t depth = frame.depth;
            printNodeLine(output, tree, child.node(), depth);
            frames.push_back(PrintFrame{.children = tree.children(child.node()), .depth = depth + 1});
        }
    }
    return output;
}

void SyntaxTreeBuilder::startNode(SyntaxKind kind) {
    openNodes.push_back(OpenNode{.kind = kind, .firstPending = pending.size()});
}

void SyntaxTreeBuilder::startNodeAt(Checkpoint place, SyntaxKind kind) {
    openNodes.push_back(OpenNode{.kind = kind, .firstPending = static_cast<std::uint32_t>(place)});
}

void SyntaxTreeBuilder::addToken(Token token) {
    const auto id = TokenId{static_cast<std::uint32_t>(tree.allTokens.size())};
    tree.allTokens.push_back(token);
    pending.push_back(SyntaxElement::ofToken(id));
    lastTokenEnd = token.range.end;
}

auto SyntaxTreeBuilder::rangeOf(SyntaxElement element) const noexcept -> TextRange {
    return element.isToken() ? tree.token(element.token()).range : tree.range(element.node());
}

void SyntaxTreeBuilder::finishNode() {
    const OpenNode open = openNodes.back();
    openNodes.pop_back();

    const auto children = std::span{pending}.subspan(open.firstPending);
    TextRange range{.start = lastTokenEnd, .end = lastTokenEnd};
    if (!children.empty()) {
        range = TextRange{.start = rangeOf(children.front()).start, .end = rangeOf(children.back()).end};
    }
    const auto firstChild = static_cast<std::uint32_t>(tree.elements.size());
    tree.elements.insert(tree.elements.end(), children.begin(), children.end());
    const auto id = NodeId{static_cast<std::uint32_t>(tree.nodes.size())};
    tree.nodes.push_back(SyntaxTree::NodeData{.kind = open.kind,
                                              .firstChild = firstChild,
                                              .childCount = static_cast<std::uint32_t>(children.size()),
                                              .range = range});
    pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(open.firstPending), pending.end());
    pending.push_back(SyntaxElement::ofNode(id));
}

auto SyntaxTreeBuilder::lastChildKind() const noexcept -> std::optional<SyntaxKind> {
    if (pending.empty() || pending.back().isToken()) {
        return std::nullopt;
    }
    return tree.kind(pending.back().node());
}

auto SyntaxTreeBuilder::finish(std::string source) && -> SyntaxTree {
    tree.rootId = pending.back().node();
    tree.sourceText = std::move(source);
    return std::move(tree);
}

} // namespace rook::compiler
