// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

export module rook.compiler:token;

import std;
import :source;

export namespace rook::compiler {

/// Every kind of token of rook/spec/grammar.ebnf. The order is that of kTokenSpellings.
enum class TokenKind : std::uint8_t {
    // What the parser reads past: it stays in the tree, which is what makes the tree lossless.
    Whitespace,
    LineComment,
    DocumentationComment,
    BlockComment,
    /// A stretch of the source the lexer already reported. The parser reads past it as well, so that
    /// one stray character is one diagnostic.
    Invalid,

    Identifier,
    Label,
    IntegerLiteral,
    FloatLiteral,
    CharacterLiteral,
    RawStringLiteral,

    // A string with its interpolations: the lexer always closes what it opens, with tokens of no
    // length where the source does not, so the parser never meets one half of a pair.
    StringStart,
    StringText,
    InterpolationStart,
    FormatSpecifier,
    InterpolationEnd,
    StringEnd,

    // Reserved words, in the order of kReservedWords.
    KeywordAbstract,
    KeywordAsync,
    KeywordAwait,
    KeywordBreak,
    KeywordClass,
    KeywordComponent,
    KeywordConst,
    KeywordContinue,
    KeywordDefer,
    KeywordDimension,
    KeywordElse,
    KeywordEnum,
    KeywordErr,
    KeywordError,
    KeywordFalse,
    KeywordFn,
    KeywordFor,
    KeywordForward,
    KeywordHandle,
    KeywordIf,
    KeywordImpl,
    KeywordImport,
    KeywordIn,
    KeywordInout,
    KeywordInterface,
    KeywordIs,
    KeywordLet,
    KeywordLoop,
    KeywordMatch,
    KeywordNarrows,
    KeywordNewtype,
    KeywordNone,
    KeywordOk,
    KeywordOpen,
    KeywordOverride,
    KeywordPersist,
    KeywordPlugin,
    KeywordPub,
    KeywordPure,
    KeywordRange,
    KeywordReadonly,
    KeywordReturn,
    KeywordSelf,
    KeywordSome,
    KeywordStruct,
    KeywordSuffix,
    KeywordSuper,
    KeywordTrue,
    KeywordType,
    KeywordVar,
    KeywordVars,
    KeywordVoid,
    KeywordWhile,

    LeftParenthesis,
    RightParenthesis,
    LeftBrace,
    RightBrace,
    LeftBracket,
    RightBracket,
    Comma,
    Semicolon,
    Colon,
    Dot,
    DotDot,
    DotDotEqual,
    Question,
    QuestionQuestion,
    Equal,
    EqualEqual,
    BangEqual,
    Less,
    LessEqual,
    Greater,
    /// Never from the lexer: the parser joins an adjacent `>` and `=` where an operator is expected,
    /// so that `List<T>=` still closes a list (rook/spec/grammar.ebnf, section 1).
    GreaterEqual,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Bang,
    AmpersandAmpersand,
    Ampersand,
    PipePipe,
    Pipe,
    At,
    Arrow,
    FatArrow,
    Underscore,

    EndOfFile,
    Count,
};

/// One token: its kind and the bytes of the source it covers.
struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    TextRange range{};
};

/// A reserved word and its token.
struct ReservedWord {
    std::string_view spelling;
    TokenKind kind;
};

/// Every reserved word, sorted by spelling so that a lookup can search it.
inline constexpr std::array kReservedWords{
    ReservedWord{"abstract", TokenKind::KeywordAbstract},
    ReservedWord{"async", TokenKind::KeywordAsync},
    ReservedWord{"await", TokenKind::KeywordAwait},
    ReservedWord{"break", TokenKind::KeywordBreak},
    ReservedWord{"class", TokenKind::KeywordClass},
    ReservedWord{"component", TokenKind::KeywordComponent},
    ReservedWord{"const", TokenKind::KeywordConst},
    ReservedWord{"continue", TokenKind::KeywordContinue},
    ReservedWord{"defer", TokenKind::KeywordDefer},
    ReservedWord{"dimension", TokenKind::KeywordDimension},
    ReservedWord{"else", TokenKind::KeywordElse},
    ReservedWord{"enum", TokenKind::KeywordEnum},
    ReservedWord{"err", TokenKind::KeywordErr},
    ReservedWord{"error", TokenKind::KeywordError},
    ReservedWord{"false", TokenKind::KeywordFalse},
    ReservedWord{"fn", TokenKind::KeywordFn},
    ReservedWord{"for", TokenKind::KeywordFor},
    ReservedWord{"forward", TokenKind::KeywordForward},
    ReservedWord{"handle", TokenKind::KeywordHandle},
    ReservedWord{"if", TokenKind::KeywordIf},
    ReservedWord{"impl", TokenKind::KeywordImpl},
    ReservedWord{"import", TokenKind::KeywordImport},
    ReservedWord{"in", TokenKind::KeywordIn},
    ReservedWord{"inout", TokenKind::KeywordInout},
    ReservedWord{"interface", TokenKind::KeywordInterface},
    ReservedWord{"is", TokenKind::KeywordIs},
    ReservedWord{"let", TokenKind::KeywordLet},
    ReservedWord{"loop", TokenKind::KeywordLoop},
    ReservedWord{"match", TokenKind::KeywordMatch},
    ReservedWord{"narrows", TokenKind::KeywordNarrows},
    ReservedWord{"newtype", TokenKind::KeywordNewtype},
    ReservedWord{"none", TokenKind::KeywordNone},
    ReservedWord{"ok", TokenKind::KeywordOk},
    ReservedWord{"open", TokenKind::KeywordOpen},
    ReservedWord{"override", TokenKind::KeywordOverride},
    ReservedWord{"persist", TokenKind::KeywordPersist},
    ReservedWord{"plugin", TokenKind::KeywordPlugin},
    ReservedWord{"pub", TokenKind::KeywordPub},
    ReservedWord{"pure", TokenKind::KeywordPure},
    ReservedWord{"range", TokenKind::KeywordRange},
    ReservedWord{"readonly", TokenKind::KeywordReadonly},
    ReservedWord{"return", TokenKind::KeywordReturn},
    ReservedWord{"self", TokenKind::KeywordSelf},
    ReservedWord{"some", TokenKind::KeywordSome},
    ReservedWord{"struct", TokenKind::KeywordStruct},
    ReservedWord{"suffix", TokenKind::KeywordSuffix},
    ReservedWord{"super", TokenKind::KeywordSuper},
    ReservedWord{"true", TokenKind::KeywordTrue},
    ReservedWord{"type", TokenKind::KeywordType},
    ReservedWord{"var", TokenKind::KeywordVar},
    ReservedWord{"vars", TokenKind::KeywordVars},
    ReservedWord{"void", TokenKind::KeywordVoid},
    ReservedWord{"while", TokenKind::KeywordWhile},
};

/// The token of a reserved word, or nothing when `text` is not one.
[[nodiscard]] auto reservedWordKind(std::string_view text) noexcept -> std::optional<TokenKind>;

/// What the parser reads past.
[[nodiscard]] constexpr auto isTrivia(TokenKind kind) noexcept -> bool {
    return kind <= TokenKind::Invalid;
}

[[nodiscard]] constexpr auto isReservedWord(TokenKind kind) noexcept -> bool {
    return kind >= TokenKind::KeywordAbstract && kind <= TokenKind::KeywordWhile;
}

} // namespace rook::compiler
