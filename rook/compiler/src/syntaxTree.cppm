// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

export module rook.compiler:syntaxTree;

import std;
import :source;
import :token;

namespace rook::compiler {

class SyntaxTreeBuilder;

} // namespace rook::compiler

export namespace rook::compiler {

/// Every kind of node of the tree, after the rules of rook/spec/grammar.ebnf. The order is that of the
/// names in syntaxTree.cpp.
enum class SyntaxKind : std::uint8_t {
    SourceFile,
    ImportDeclaration,
    ImportList,
    PluginDeclaration,
    Attribute,

    FunctionDeclaration,
    ForwardDeclaration,
    ParameterList,
    Parameter,
    GenericParameterList,
    GenericParameter,
    StructDeclaration,
    ComponentDeclaration,
    FieldList,
    Field,
    EnumDeclaration,
    Variant,
    VariantField,
    ClassDeclaration,
    InterfaceDeclaration,
    ImplementationDeclaration,
    MemberList,
    MemberVariable,
    NewtypeDeclaration,
    SuffixClause,
    DimensionClause,
    RangeClause,
    DeriveClause,
    HandleDeclaration,
    VarsDeclaration,
    VarsField,
    ConstantDeclaration,
    GlobalDeclaration,
    TypeAlias,

    PathType,
    GenericArgumentList,
    TupleType,
    ArrayType,
    FunctionType,
    OptionalType,

    Block,
    LetStatement,
    VarStatement,
    AssignmentStatement,
    ExpressionStatement,
    WhileStatement,
    ForStatement,
    LoopStatement,
    DeferStatement,
    ReturnStatement,
    BreakStatement,
    ContinueStatement,

    LiteralExpression,
    StringExpression,
    Interpolation,
    NameExpression,
    ImplicitMemberExpression,
    MemberExpression,
    CallExpression,
    ArgumentList,
    Argument,
    IndexExpression,
    TryExpression,
    PrefixExpression,
    AwaitExpression,
    BinaryExpression,
    RangeExpression,
    IsExpression,
    ParenthesizedExpression,
    TupleExpression,
    ArrayExpression,
    ClosureExpression,
    ClosureParameterList,
    StructLiteral,
    FieldInitializerList,
    FieldInitializer,
    IfExpression,
    LetCondition,
    MatchExpression,
    MatchArm,
    MatchGuard,

    WildcardPattern,
    LiteralPattern,
    WrapperPattern,
    NamePattern,
    MemberPattern,
    TuplePattern,
    OrPattern,
    PatternFieldList,
    PatternField,

    /// Source that fits no rule, kept so that the tree stays lossless. A diagnostic always says why.
    Error,

    Count,
};

/// The node of a tree, by its place in it.
enum class NodeId : std::uint32_t {};

/// The token of a tree, by its place in the source: the tokens of a tree are in source order.
enum class TokenId : std::uint32_t {};

/// One child of a node: another node or a token.
class SyntaxElement {
public:
    [[nodiscard]] static constexpr auto ofNode(NodeId id) noexcept -> SyntaxElement {
        return SyntaxElement{static_cast<std::uint32_t>(id)};
    }

    [[nodiscard]] static constexpr auto ofToken(TokenId id) noexcept -> SyntaxElement {
        return SyntaxElement{static_cast<std::uint32_t>(id) | kTokenBit};
    }

    [[nodiscard]] constexpr auto isToken() const noexcept -> bool { return (value & kTokenBit) != 0; }

    /// The node this element is; a token's element must not ask.
    [[nodiscard]] constexpr auto node() const noexcept -> NodeId { return NodeId{value}; }

    /// The token this element is; a node's element must not ask.
    [[nodiscard]] constexpr auto token() const noexcept -> TokenId { return TokenId{value & ~kTokenBit}; }

private:
    static constexpr std::uint32_t kTokenBit = 0x8000'0000U;

    explicit constexpr SyntaxElement(std::uint32_t elementValue) noexcept : value{elementValue} {}

    std::uint32_t value;
};

/// A lossless concrete syntax tree: it owns its source, and its tokens, whitespace and comments
/// included, cover every byte of it, so that the formatter and refactorings can write it back as it
/// was. Nodes and tokens live in flat arrays: a tree of any size is destroyed without recursion.
class SyntaxTree {
public:
    [[nodiscard]] auto source() const noexcept -> std::string_view { return sourceText; }

    [[nodiscard]] auto root() const noexcept -> NodeId { return rootId; }

    /// Every token in source order.
    [[nodiscard]] auto tokens() const noexcept -> std::span<const Token> { return allTokens; }

    [[nodiscard]] auto token(TokenId id) const noexcept -> const Token& {
        return allTokens[static_cast<std::uint32_t>(id)];
    }

    [[nodiscard]] auto kind(NodeId id) const noexcept -> SyntaxKind {
        return nodes[static_cast<std::uint32_t>(id)].kind;
    }

    /// The bytes a node covers, from its first token to its last.
    [[nodiscard]] auto range(NodeId id) const noexcept -> TextRange {
        return nodes[static_cast<std::uint32_t>(id)].range;
    }

    /// A node's children in source order.
    [[nodiscard]] auto children(NodeId id) const noexcept -> std::span<const SyntaxElement> {
        const NodeData& node = nodes[static_cast<std::uint32_t>(id)];
        return std::span{elements}.subspan(node.firstChild, node.childCount);
    }

    /// The text of a range of this tree's source.
    [[nodiscard]] auto text(TextRange textRange) const noexcept -> std::string_view {
        return std::string_view{sourceText}.substr(textRange.start, textRange.end - textRange.start);
    }

private:
    friend class SyntaxTreeBuilder;

    struct NodeData {
        SyntaxKind kind = SyntaxKind::Error;
        std::uint32_t firstChild = 0;
        std::uint32_t childCount = 0;
        TextRange range{};
    };

    std::string sourceText;
    std::vector<Token> allTokens;
    std::vector<NodeData> nodes;
    std::vector<SyntaxElement> elements;
    NodeId rootId{};
};

/// The name of a kind of node, as printSyntaxTree writes it.
[[nodiscard]] auto syntaxKindName(SyntaxKind kind) noexcept -> std::string_view;

/// The tree as text, one node on each line, indented by its depth, with the tokens directly under it
/// other than whitespace and plain comments: what the conformance suite compares.
[[nodiscard]] auto printSyntaxTree(const SyntaxTree& tree) -> std::string;

} // namespace rook::compiler

namespace rook::compiler {

/// Builds a tree while the parser reads, in the order of the source. A node may be opened at a place
/// already passed, which wraps what came since: `a + b` is known to be a sum only after `a`.
class SyntaxTreeBuilder {
public:
    /// A place in the children of the node open now, to open a node at later.
    enum class Checkpoint : std::uint32_t {};

    [[nodiscard]] auto checkpoint() const noexcept -> Checkpoint {
        return Checkpoint{static_cast<std::uint32_t>(pending.size())};
    }

    void startNode(SyntaxKind kind);
    void startNodeAt(Checkpoint place, SyntaxKind kind);
    void finishNode();
    void addToken(Token token);

    /// The kind of the last child added to the open node, if it is a node.
    [[nodiscard]] auto lastChildKind() const noexcept -> std::optional<SyntaxKind>;

    /// The tree, once every node opened has finished and the last one is its root.
    [[nodiscard]] auto finish(std::string source) && -> SyntaxTree;

private:
    struct OpenNode {
        SyntaxKind kind;
        std::size_t firstPending;
    };

    [[nodiscard]] auto rangeOf(SyntaxElement element) const noexcept -> TextRange;

    SyntaxTree tree;
    std::vector<OpenNode> openNodes;
    /// The finished children of every node still open, innermost last.
    std::vector<SyntaxElement> pending;
    /// Where the last token ended: where a node with no token stands.
    ByteOffset lastTokenEnd = 0;
};

} // namespace rook::compiler
