// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.ui;

import std;
import nx.core;
import nx.console;
import nx.platform;
import nx.render;

namespace nx::ui {

namespace {

/// The pixels between the window's edges and its parts, and between its parts.
constexpr std::int32_t kSpacing = 4;
/// What the console shows before a line typed in it, as the original's does.
constexpr std::string_view kTypedMark = "] ";

/// The names the console offers, drawn over its output: each name, then what the console tells of
/// it; the chosen one over a band of its own.
class OfferedNames final : public Panel {
public:
    OfferedNames(const std::vector<console::Completion>& offered, const std::optional<std::size_t>& chosen,
                 const TextFace& face, const ConsoleColors& colors)
        : Panel{"OfferedNames"}, m_offered{&offered}, m_chosen{&chosen}, m_face{&face}, m_colors{&colors} {}

protected:
    void paint(PanelPainter& painter) const override {
        Panel::paint(painter);
        if (m_face->atlas == nullptr) {
            return;
        }
        const std::int32_t height = m_face->atlas->metrics().height();
        for (std::size_t index = 0; index < m_offered->size(); ++index) {
            const console::Completion& name = (*m_offered)[index];
            const std::int32_t top = kSpacing / 2 + (narrow<std::int32_t>(index) * height);
            if (*m_chosen == index) {
                painter.fill(0, top, bounds().wide, height, m_colors->chosen);
            }
            const std::int32_t end = painter.text(*m_face, name.name, kSpacing, top, m_colors->offered);
            const std::string_view told =
                !name.value.empty() ? std::string_view{name.value} : name.description;
            const std::string detail =
                name.expectation.empty() ? std::string{told} : std::format("{} ({})", told, name.expectation);
            std::ignore = painter.text(*m_face, detail, end + (2 * kSpacing), top, m_colors->detail);
        }
    }

private:
    const std::vector<console::Completion>* m_offered;
    const std::optional<std::size_t>* m_chosen;
    const TextFace* m_face;
    const ConsoleColors* m_colors;
};

} // namespace

ConsoleDialog::ConsoleDialog(console::Console& console, PanelBounds bounds)
    : Panel{"ConsoleDialog", bounds}, m_console{&console} {
    m_output = &static_cast<RichText&>(add(std::make_unique<RichText>("ConsoleHistory")));
    m_input = &static_cast<TextEntry&>(add(std::make_unique<TextEntry>("ConsoleEntry")));
    m_names = &add(std::make_unique<OfferedNames>(m_offered, m_chosen, m_face, m_colors));
    m_names->setVisible(false);
    m_output->setAnchors(PinCorner::TopLeft, AutoResize::DownAndRight);
    m_input->setAnchors(PinCorner::BottomLeft, AutoResize::Right);
    m_names->setAnchors(PinCorner::BottomLeft, AutoResize::Right);
    setColors(m_colors);
    layOut();
}

void ConsoleDialog::setFace(TextFace face) {
    m_face = face;
    m_output->setFace(face);
    m_input->setFace(face);
    layOut();
}

void ConsoleDialog::setColors(const ConsoleColors& colors) {
    m_colors = colors;
    setBackground(colors.background);
    m_input->setColors(colors.input, colors.cursor);
    m_names->setBackground(colors.offeredBackground);
}

void ConsoleDialog::layOut() {
    const std::int32_t line = m_face.atlas != nullptr ? m_face.atlas->metrics().height() : 0;
    const std::int32_t inputTall = line + kSpacing;
    const std::int32_t wide = bounds().wide - (2 * kSpacing);
    const std::int32_t inputTop = bounds().tall - kSpacing - inputTall;
    m_input->setBounds({.x = kSpacing, .y = inputTop, .wide = wide, .tall = inputTall});
    m_input->setTextInset(kSpacing / 2);
    m_output->setBounds({.x = kSpacing, .y = kSpacing, .wide = wide, .tall = inputTop - (2 * kSpacing)});
    const std::int32_t namesTall = (narrow<std::int32_t>(m_offered.size()) * line) + kSpacing;
    m_names->setBounds({.x = kSpacing, .y = inputTop - namesTall, .wide = wide, .tall = namesTall});
    m_names->setVisible(!m_offered.empty());
}

void ConsoleDialog::offerNames() {
    const std::string line = m_input->text();
    const std::optional<console::CompletionPrefix> typing = console::completionPrefix(line);
    m_offered = typing && !typing->prefix.empty()
                    ? m_console->completions(typing->prefix, kConsoleCompletions)
                    : std::vector<console::Completion>{};
    m_chosen.reset();
    layOut();
}

auto ConsoleDialog::pressKey(platform::Key key) -> bool {
    switch (key) {
    case platform::Key::Enter:
    case platform::Key::KeypadEnter: {
        const std::string line = m_input->text();
        m_output->append({.text = std::format("{}{}", kTypedMark, line), .color = m_colors.typed});
        m_output->scrollBy(-narrow<std::int64_t>(m_output->scrolled())); // back to the newest
        m_history.add(line);
        m_console->addText(line);
        m_input->setText({});
        offerNames();
        return true;
    }
    case platform::Key::Tab: {
        const std::string line = m_input->text();
        const std::optional<console::CompletionPrefix> typing = console::completionPrefix(line);
        if (!typing || m_offered.empty()) {
            return true;
        }
        const console::Completion& taken = m_offered[m_chosen.value_or(0)];
        m_input->setText(std::format("{}{} ", std::string_view{line}.substr(0, typing->offset), taken.name));
        offerNames();
        return true;
    }
    case platform::Key::Up:
    case platform::Key::Down: {
        const bool isUp = key == platform::Key::Up;
        if (!m_offered.empty()) {
            const std::size_t last = m_offered.size() - 1;
            m_chosen = !m_chosen ? (isUp ? last : 0)
                       : isUp    ? (*m_chosen == 0 ? last : *m_chosen - 1)
                                 : (*m_chosen == last ? 0 : *m_chosen + 1);
            return true;
        }
        const std::optional<std::string_view> walked = isUp ? m_history.older() : m_history.newer();
        m_input->setText(walked.value_or(std::string_view{}));
        return true;
    }
    case platform::Key::PageUp:
    case platform::Key::PageDown: {
        const auto half = narrow<std::int64_t>(std::max<std::size_t>(m_output->visibleRows() / 2, 1));
        m_output->scrollBy(key == platform::Key::PageUp ? half : -half);
        return true;
    }
    default:
        if (m_input->pressKey(key) == TextEntryAction::None) {
            return false;
        }
        offerNames();
        return true;
    }
}

void ConsoleDialog::insertText(std::string_view text) {
    m_input->insertText(text);
    offerNames();
}

void ConsoleDialog::showLogged(log::Level level, std::string_view line) {
    const render::ScreenColor color = level >= log::Level::Error     ? m_colors.error
                                      : level == log::Level::Warning ? m_colors.warning
                                      : level == log::Level::Info    ? m_colors.reply
                                                                     : m_colors.debug;
    m_output->append({.text = std::string{line}, .color = color});
}

void ConsoleDialog::print(std::string_view line) {
    m_output->append({.text = std::string{line}, .color = m_colors.reply});
}

void ConsoleDialog::reportFailure(const Failure<console::ConsoleError>& failure) {
    std::string text = failure.detail.empty() ? std::string{enumName(failure.code)} : failure.detail;
    for (const CauseRecord& cause : failure.cause.records()) {
        text += std::format(" ({}: {})", cause.code, cause.detail);
    }
    m_output->append({.text = std::move(text), .color = m_colors.error});
}

void ConsoleLogSink::write(const log::Record& record) noexcept {
    const std::scoped_lock lock{m_mutex};
    if (m_lines.size() == kRichTextLines) {
        m_lines.pop_front(); // a window shows no more
    }
    m_lines.emplace_back(record.level, std::format("[{}] {}", record.category, record.message));
}

void ConsoleLogSink::showIn(ConsoleDialog& dialog) {
    std::deque<std::pair<log::Level, std::string>> lines;
    {
        const std::scoped_lock lock{m_mutex};
        lines.swap(m_lines);
    }
    for (const auto& [level, line] : lines) {
        dialog.showLogged(level, line);
    }
}

} // namespace nx::ui
