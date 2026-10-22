// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The console's window (ADR-0020, docs/12-ui-hud-and-console.md, section 5): its output, the lines
/// typed and what the console replies and logs, each in the colour of its kind; its input line,
/// with the history the up and down keys walk; and the names the line being typed completes to,
/// with what the console tells of them, which Tab takes.
export module nx.ui:consoleDialog;

import std;
import nx.core;
import nx.console;
import nx.platform;
import nx.render;
import :panel;
import :painter;
import :text;
import :textEntry;
import :richText;

export namespace nx::ui {

/// How many names the console offers at once.
inline constexpr std::size_t kConsoleCompletions = 8;

/// The colours the console draws in.
struct ConsoleColors {
    render::ScreenColor background{.red = 0, .green = 0, .blue = 0, .alpha = 192};
    render::ScreenColor reply{.red = 216, .green = 222, .blue = 211};  ///< Replies, and what is logged.
    render::ScreenColor typed{.red = 255, .green = 255, .blue = 255};  ///< The lines typed, after "] ".
    render::ScreenColor debug{.red = 140, .green = 140, .blue = 140};  ///< Logged at Debug and Trace.
    render::ScreenColor warning{.red = 255, .green = 200, .blue = 64}; ///< Logged at Warning.
    render::ScreenColor error{.red = 255, .green = 96, .blue = 80};    ///< Logged at Error, and failures.
    render::ScreenColor input{.red = 255, .green = 255, .blue = 255};  ///< The line being typed.
    render::ScreenColor cursor{.red = 216, .green = 222, .blue = 211};
    render::ScreenColor offered{.red = 216, .green = 222, .blue = 211}; ///< A name offered.
    render::ScreenColor detail{.red = 140, .green = 140, .blue = 140};  ///< What the console tells of it.
    render::ScreenColor chosen{
        .red = 64, .green = 64, .blue = 64, .alpha = 224}; ///< Behind the one Tab takes.
};

/// The console's window over `console`: it runs the lines typed in it, through the console's buffer,
/// and shows what the console replies (it is the console's output) and what is logged.
class ConsoleDialog : public Panel, public console::ConsoleOutput {
public:
    /// A console window at `bounds`; `console` outlives it.
    ConsoleDialog(console::Console& console, PanelBounds bounds);

    /// The face its text is in; its parts are laid out by its line.
    void setFace(TextFace face);
    void setColors(const ConsoleColors& colors);

    /// What `key` does in the console:
    /// - Enter shows the line after "] ", keeps it in the history and queues it on the console's
    ///   buffer (the console runs it in its turn), then empties the line;
    /// - Tab completes the name being typed to the name offered first, or chosen, and a space;
    /// - Up and Down choose among the names offered, else walk the history;
    /// - Page Up and Page Down scroll the output by half its rows;
    /// - the keys that edit a line edit the input line.
    /// Returns whether the console took the key: Escape and the rest are its owner's.
    auto pressKey(platform::Key key) -> bool;
    /// Types `text` (UTF-8) at the input line's cursor.
    void insertText(std::string_view text);

    /// Shows `line`, logged at `level`, in that level's colour.
    void showLogged(log::Level level, std::string_view line);
    /// A reply of the console.
    void print(std::string_view line) override;
    /// A command of the console that failed, in the error colour.
    void reportFailure(const Failure<console::ConsoleError>& failure) override;

    [[nodiscard]] inline auto output() const noexcept -> const RichText& { return *m_output; }
    [[nodiscard]] inline auto input() const noexcept -> const TextEntry& { return *m_input; }
    /// The names the line being typed completes to, at most kConsoleCompletions.
    [[nodiscard]] inline auto offered() const noexcept -> std::span<const console::Completion> {
        return m_offered;
    }
    /// The name offered that Tab takes when the player chose one with Up or Down.
    [[nodiscard]] inline auto chosen() const noexcept -> std::optional<std::size_t> { return m_chosen; }

private:
    /// Offers again the names the line being typed completes to.
    void offerNames();
    /// Places the output, the input line and the names offered by the face's line.
    void layOut();

    console::Console* m_console;
    console::CommandHistory m_history;
    RichText* m_output = nullptr; ///< Its children.
    TextEntry* m_input = nullptr;
    Panel* m_names = nullptr;
    std::vector<console::Completion> m_offered;
    std::optional<std::size_t> m_chosen;
    TextFace m_face{};
    ConsoleColors m_colors{};
};

/// The lines logged from any thread for a console window: a sink of the log that keeps them until
/// the main thread shows them.
class ConsoleLogSink final : public log::Sink {
public:
    void write(const log::Record& record) noexcept override;
    /// Shows in `dialog` the lines logged since the last call, in their order.
    void showIn(ConsoleDialog& dialog);

private:
    std::mutex m_mutex;
    std::vector<std::pair<log::Level, std::string>> m_lines; ///< Guarded by m_mutex.
};

} // namespace nx::ui
