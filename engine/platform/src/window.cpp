// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module;

#include <SDL3/SDL.h>

module nx.platform;

import std;
import nx.core;

namespace nx::platform {

namespace {

/// The scancode of `key`: Key is the HID usage code, as SDL's scancodes are, so they convert by value.
[[nodiscard]] constexpr auto scancodeOf(Key key) noexcept -> SDL_Scancode {
    return static_cast<SDL_Scancode>(std::to_underlying(key));
}

static_assert(scancodeOf(Key::A) == SDL_SCANCODE_A && scancodeOf(Key::Digit0) == SDL_SCANCODE_0 &&
              scancodeOf(Key::Semicolon) == SDL_SCANCODE_SEMICOLON &&
              scancodeOf(Key::KeypadPeriod) == SDL_SCANCODE_KP_PERIOD &&
              scancodeOf(Key::Menu) == SDL_SCANCODE_APPLICATION &&
              scancodeOf(Key::LeftControl) == SDL_SCANCODE_LCTRL &&
              scancodeOf(Key::RightSystem) == SDL_SCANCODE_RGUI);
static_assert(std::to_underlying(MouseButton::Left) == SDL_BUTTON_LEFT &&
              std::to_underlying(MouseButton::Middle) == SDL_BUTTON_MIDDLE &&
              std::to_underlying(MouseButton::Right) == SDL_BUTTON_RIGHT &&
              std::to_underlying(MouseButton::X2) == SDL_BUTTON_X2);

[[nodiscard]] auto windowOf(void* window) noexcept -> SDL_Window* {
    return static_cast<SDL_Window*>(window);
}

/// SDL's message about its last failure.
[[nodiscard]] auto sdlError() -> std::string {
    return SDL_GetError();
}

/// Converts a display mode's screen coordinates to drawable pixels.
[[nodiscard]] auto pixelsOf(const SDL_DisplayMode& mode) noexcept -> PixelSize {
    return {.width = narrow<std::uint32_t>(std::lround(static_cast<float>(mode.w) * mode.pixel_density)),
            .height = narrow<std::uint32_t>(std::lround(static_cast<float>(mode.h) * mode.pixel_density))};
}

/// The modes reported by SDL, in its preferred order within a resolution.
using DisplayModes = std::unique_ptr<SDL_DisplayMode*, decltype(&SDL_free)>;

/// How many windows SDL has open; its video subsystem must be started.
[[nodiscard]] auto openWindowCount() noexcept -> int {
    int count = 0;
    SDL_free(SDL_GetWindows(&count));
    return count;
}

/// SDL's base character, as opposed to its flagged codes for keys that do not type characters.
[[nodiscard]] auto characterOf(SDL_Keycode code) noexcept -> std::optional<char32_t> {
    if ((code & (SDLK_SCANCODE_MASK | SDLK_EXTENDED_MASK)) != 0 || code < SDLK_SPACE || code == SDLK_DELETE) {
        return std::nullopt;
    }
    return static_cast<char32_t>(code);
}

/// The event `event` is, or nullopt for events the window does not report.
[[nodiscard]] auto translate(const SDL_Event& event) -> std::optional<WindowEvent> {
    switch (static_cast<SDL_EventType>(event.type)) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        return CloseRequested{};
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        return Resized{.size = {.width = narrow<std::uint32_t>(std::max(event.window.data1, 0)),
                                .height = narrow<std::uint32_t>(std::max(event.window.data2, 0))}};
    case SDL_EVENT_WINDOW_MINIMIZED:
        return Minimized{.isMinimized = true};
    case SDL_EVENT_WINDOW_RESTORED:
        return Minimized{.isMinimized = false};
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        return FocusChanged{.isFocused = true};
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        return FocusChanged{.isFocused = false};
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        return KeyChanged{.key = Key{narrow<std::uint16_t>(std::to_underlying(event.key.scancode))},
                          .isDown = event.key.down,
                          .isRepeat = event.key.repeat,
                          .character = characterOf(event.key.key)};
    case SDL_EVENT_MOUSE_MOTION:
        return MouseMoved{.dx = event.motion.xrel, .dy = event.motion.yrel};
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return MouseButtonChanged{.button = MouseButton{event.button.button}, .isDown = event.button.down};
    case SDL_EVENT_MOUSE_WHEEL:
        // A flipped wheel ("natural scrolling") reports the opposite of the finger's motion.
        return MouseWheelMoved{.dy = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y
                                                                                     : event.wheel.y};
    case SDL_EVENT_TEXT_INPUT:
        return TextEntered{.text = event.text.text != nullptr ? std::string{event.text.text} : std::string{}};
    default:
        return std::nullopt;
    }
}

} // namespace

auto Window::create(const WindowDesc& desc) -> Result<Window, WindowError> {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        return fail(WindowError::VideoUnavailable,
                    std::format("SDL_InitSubSystem(SDL_INIT_VIDEO): {}", sdlError()));
    }
    // SDL queues the events of every window together: a second window would take the first's.
    expects(openWindowCount() == 0, "Window::create: a program has one window, which takes every event");
    const std::string title{desc.title}; // SDL wants it zero-terminated
    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (desc.isResizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (!desc.isShown) {
        flags |= SDL_WINDOW_HIDDEN;
    }
    SDL_Window* window =
        SDL_CreateWindow(title.c_str(), narrow<int>(desc.size.width), narrow<int>(desc.size.height), flags);
    if (window == nullptr) {
        std::string error = sdlError();
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return fail(WindowError::CreateFailed, std::format("SDL_CreateWindow: {}", error));
    }
    return Window{window};
}

void Window::close() noexcept {
    if (m_window == nullptr) {
        return;
    }
    SDL_DestroyWindow(windowOf(std::exchange(m_window, nullptr)));
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

auto Window::pixelSize() const -> Result<PixelSize, WindowError> {
    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(windowOf(m_window), &width, &height)) {
        return fail(WindowError::SizeUnavailable, std::format("SDL_GetWindowSizeInPixels: {}", sdlError()));
    }
    return PixelSize{.width = narrow<std::uint32_t>(std::max(width, 0)),
                     .height = narrow<std::uint32_t>(std::max(height, 0))};
}

auto Window::nativeWindow() const -> Result<NativeWindow, WindowError> {
    const SDL_PropertiesID properties = SDL_GetWindowProperties(windowOf(m_window));
    const char* driverName = SDL_GetCurrentVideoDriver();
    const std::string_view driver = driverName == nullptr ? std::string_view{} : std::string_view{driverName};
    if (driver == "windows") {
        return Win32Window{
            .instance = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr),
            .window = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)};
    }
    if (driver == "x11") {
        return XlibWindow{
            .display = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr),
            .window = narrow<std::uint64_t>(
                SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0))};
    }
    if (driver == "wayland") {
        return WaylandWindow{
            .display = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr),
            .surface = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr)};
    }
    return fail(WindowError::NoNativeWindow,
                std::format("the {:?} video driver has no window a GPU can draw to", driver));
}

auto Window::show() -> Result<void, WindowError> {
    if (!SDL_ShowWindow(windowOf(m_window))) {
        return fail(WindowError::ShowFailed, std::format("SDL_ShowWindow: {}", sdlError()));
    }
    return {};
}

auto Window::isShown() const noexcept -> bool {
    return (SDL_GetWindowFlags(windowOf(m_window)) & SDL_WINDOW_HIDDEN) == 0;
}

auto Window::mode() const noexcept -> WindowMode {
    SDL_Window* window = windowOf(m_window);
    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == 0) {
        return WindowMode::Windowed;
    }
    return SDL_GetWindowFullscreenMode(window) == nullptr ? WindowMode::Borderless : WindowMode::Fullscreen;
}

auto Window::displayResolutions() const -> std::vector<PixelSize> {
    const SDL_DisplayID display = SDL_GetDisplayForWindow(windowOf(m_window));
    int count = 0;
    const DisplayModes modes{SDL_GetFullscreenDisplayModes(display, &count), SDL_free};
    std::vector<PixelSize> resolutions;
    if (modes) {
        for (const SDL_DisplayMode* mode : std::span{modes.get(), static_cast<std::size_t>(count)}) {
            resolutions.push_back(pixelsOf(*mode));
        }
    }
    if (const auto* desktop = SDL_GetDesktopDisplayMode(display)) {
        resolutions.push_back(pixelsOf(*desktop));
    }
    std::ranges::sort(resolutions, {}, [](PixelSize size) { return std::pair{size.width, size.height}; });
    resolutions.erase(std::ranges::unique(resolutions).begin(), resolutions.end());
    return resolutions;
}

auto Window::setMode(WindowMode requested, std::optional<PixelSize> resolution) -> Result<void, WindowError> {
    SDL_Window* const window = windowOf(m_window);
    if (requested == WindowMode::Windowed) {
        if (mode() != WindowMode::Windowed &&
            (!SDL_SetWindowFullscreen(window, false) || !SDL_SyncWindow(window))) {
            return fail(WindowError::ModeUnavailable, std::format("leaving fullscreen: {}", sdlError()));
        }
        const PixelSize size = resolution.value_or(kDefaultWindowSize);
        const float density = SDL_GetWindowPixelDensity(window);
        if (density <= 0.0F ||
            !SDL_SetWindowSize(window, narrow<int>(std::lround(static_cast<float>(size.width) / density)),
                               narrow<int>(std::lround(static_cast<float>(size.height) / density)))) {
            return fail(WindowError::ModeUnavailable, std::format("setting the window size: {}", sdlError()));
        }
    } else {
        const SDL_DisplayMode* exclusive = nullptr;
        int count = 0;
        const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
        const DisplayModes modes{requested == WindowMode::Fullscreen && resolution
                                     ? SDL_GetFullscreenDisplayModes(display, &count)
                                     : nullptr,
                                 SDL_free};
        if (requested == WindowMode::Fullscreen) {
            exclusive = SDL_GetDesktopDisplayMode(display);
            if (resolution && (!exclusive || pixelsOf(*exclusive) != *resolution)) {
                exclusive = nullptr;
                if (modes) {
                    for (const auto* mode : std::span{modes.get(), static_cast<std::size_t>(count)}) {
                        if (pixelsOf(*mode) == *resolution) {
                            exclusive = mode;
                            break;
                        }
                    }
                }
            }
            if (exclusive == nullptr) {
                return fail(WindowError::ModeUnavailable,
                            "the display does not support the fullscreen resolution");
            }
        }
        if (!SDL_SetWindowFullscreenMode(window, exclusive) || !SDL_SetWindowFullscreen(window, true)) {
            return fail(WindowError::ModeUnavailable, std::format("fullscreen transition: {}", sdlError()));
        }
    }
    if (!SDL_SyncWindow(window)) {
        return fail(WindowError::ModeUnavailable, std::format("SDL_SyncWindow: {}", sdlError()));
    }
    if (mode() != requested) {
        return fail(WindowError::ModeUnavailable, "the windowing system did not apply the requested mode");
    }
    return {};
}

auto Window::mousePosition() const noexcept -> MousePosition {
    SDL_Window* const window = windowOf(m_window);
    MousePosition position;
    // SDL tells it in the window's own units, which a desktop's scaling makes fewer than its pixels.
    std::ignore = SDL_GetMouseState(&position.x, &position.y);
#ifdef _WIN32
    // The cached mouse position can be stale after a fullscreen switch until SDL gets mouse motion.
    // Read the desktop position directly while fullscreen menus use absolute coordinates.
    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0 &&
        !SDL_GetWindowRelativeMouseMode(window)) {
        int x = 0;
        int y = 0;
        if (SDL_GetWindowPosition(window, &x, &y)) {
            std::ignore = SDL_GetGlobalMouseState(&position.x, &position.y);
            position.x -= static_cast<float>(x);
            position.y -= static_cast<float>(y);
        }
    }
#endif
    int width = 0;
    int height = 0;
    const auto pixels = pixelSize();
    if (SDL_GetWindowSize(window, &width, &height) && pixels && width > 0 && height > 0) {
        position.x *= static_cast<float>(pixels->width) / static_cast<float>(width);
        position.y *= static_cast<float>(pixels->height) / static_cast<float>(height);
    }
    return position;
}

auto Window::setMouseMode(MouseMode mode, bool redrawCursor) -> Result<void, WindowError> {
    SDL_Window* const window = windowOf(m_window);
    const bool relative = mode == MouseMode::Relative;
    if (SDL_GetWindowRelativeMouseMode(window) != relative &&
        !SDL_SetWindowRelativeMouseMode(window, relative)) {
        return fail(WindowError::RelativeMouseUnavailable,
                    std::format("SDL_SetWindowRelativeMouseMode: {}", sdlError()));
    }
    const bool showNativeCursor = mode == MouseMode::Absolute;
    if (!relative && SDL_CursorVisible() != showNativeCursor &&
        !(showNativeCursor ? SDL_ShowCursor() : SDL_HideCursor())) {
        return fail(WindowError::CursorUnavailable, sdlError());
    }
    if (showNativeCursor && redrawCursor && !SDL_SetCursor(nullptr)) {
        return fail(WindowError::CursorUnavailable, sdlError());
    }
    return {};
}

auto Window::setTextInput(TextInput input) -> Result<void, WindowError> {
    const bool isOn = input == TextInput::On;
    if (!(isOn ? SDL_StartTextInput(windowOf(m_window)) : SDL_StopTextInput(windowOf(m_window)))) {
        return fail(WindowError::TextInputUnavailable,
                    std::format("{}: {}", isOn ? "SDL_StartTextInput" : "SDL_StopTextInput", sdlError()));
    }
    return {};
}

auto Window::isTextInputOn() const noexcept -> bool {
    return SDL_TextInputActive(windowOf(m_window));
}

void Window::pollEvents(std::vector<WindowEvent>& events) {
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        if (auto translated = translate(event)) {
            events.push_back(*translated);
        }
    }
}

} // namespace nx::platform
