// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The program's window and its input, over SDL3 (docs/12-ui-hud-and-console.md, section 6): keys
/// by their physical position, the mouse as raw relative motion, and the native handles a GPU
/// backend draws to. SDL stays in the implementation: nothing of it crosses this interface.
export module nx.platform:window;

import std;
import nx.core;

export namespace nx::platform {

/// What can go wrong with the window. The detail has SDL's message.
enum class WindowError : std::uint8_t {
    VideoUnavailable,         ///< The video subsystem did not start (no display, no driver).
    CreateFailed,             ///< The window could not be created.
    NoNativeWindow,           ///< The video driver has no window a GPU can draw to (offscreen).
    RelativeMouseUnavailable, ///< The mouse could not be captured for relative motion.
};

/// A size in pixels.
struct PixelSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    [[nodiscard]] inline auto operator==(const PixelSize&) const noexcept -> bool = default;
};

/// A key by its physical position, whatever the keyboard layout: the USB HID usage code (page 7),
/// which SDL's scancodes are. A key without an enumerator (international keys, media keys) keeps
/// its code.
enum class Key : std::uint16_t {
    A = 4,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
    Digit0,
    Enter,
    Escape,
    Backspace,
    Tab,
    Space,
    Minus,
    Equals,
    LeftBracket,
    RightBracket,
    Backslash,
    Semicolon = 51,
    Apostrophe,
    Grave,
    Comma,
    Period,
    Slash,
    CapsLock,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    PrintScreen,
    ScrollLock,
    Pause,
    Insert,
    Home,
    PageUp,
    Delete,
    End,
    PageDown,
    Right,
    Left,
    Down,
    Up,
    NumLock,
    KeypadDivide,
    KeypadMultiply,
    KeypadMinus,
    KeypadPlus,
    KeypadEnter,
    Keypad1,
    Keypad2,
    Keypad3,
    Keypad4,
    Keypad5,
    Keypad6,
    Keypad7,
    Keypad8,
    Keypad9,
    Keypad0,
    KeypadPeriod,
    Menu = 101,
    LeftControl = 224,
    LeftShift,
    LeftAlt,
    LeftSystem, ///< Windows, Command, Super.
    RightControl,
    RightShift,
    RightAlt,
    RightSystem,
};

/// A mouse button by its number: 1 to 5 are the original's MOUSE1 (left), MOUSE3 (middle),
/// MOUSE2 (right), MOUSE4 and MOUSE5. Buttons past 5 keep their number.
enum class MouseButton : std::uint8_t {
    Left = 1,
    Middle,
    Right,
    X1, ///< Usually "back".
    X2, ///< Usually "forward".
};

/// The user closed the window, or the program was asked to end (Ctrl+C in its console).
struct CloseRequested {};

/// The drawable size changed: what a swapchain must match.
struct Resized {
    PixelSize size;
};

/// The window was minimized, or restored from it: a minimized window has nothing to draw to.
struct Minimized {
    bool isMinimized = false;
};

struct FocusChanged {
    bool isFocused = false;
};

struct KeyChanged {
    Key key{};
    bool isDown = false;
    bool isRepeat = false; ///< Sent again while held, by the system's key repeat.
};

/// Relative motion in the mouse's own counts, without the system's acceleration while the mouse is
/// relative (Window::setRelativeMouse): what `sensitivity` scales.
struct MouseMoved {
    float dx = 0.0F;
    float dy = 0.0F; ///< Positive toward the user.
};

struct MouseButtonChanged {
    MouseButton button{};
    bool isDown = false;
};

/// Wheel notches, fractional on precise wheels: positive away from the user (MWHEELUP).
struct MouseWheelMoved {
    float dy = 0.0F;
};

using WindowEvent = std::variant<CloseRequested, Resized, Minimized, FocusChanged, KeyChanged, MouseMoved,
                                 MouseButtonChanged, MouseWheelMoved>;

/// A Win32 window: the HINSTANCE and HWND a Vulkan or D3D11 surface is made from.
struct Win32Window {
    void* instance = nullptr;
    void* window = nullptr;
};

/// An X11 window: the Display* and the Window id.
struct XlibWindow {
    void* display = nullptr;
    std::uint64_t window = 0;
};

/// A Wayland window: the wl_display* and the wl_surface*.
struct WaylandWindow {
    void* display = nullptr;
    void* surface = nullptr;
};

/// What a GPU backend makes its surface from; the backend creates the surface itself.
using NativeWindow = std::variant<Win32Window, XlibWindow, WaylandWindow>;

struct WindowDesc {
    std::string_view title;
    PixelSize size{.width = 1280, .height = 720}; ///< Of the drawable area, before the desktop's scaling.
    bool isResizable = true;
};

/// The program's window. There is one at a time, and it takes every event of the program: SDL
/// queues them all together. Use it from the main thread only (docs/04-job-system.md, section 5).
class Window {
public:
    /// Starts the video subsystem (with the window's lifetime) and opens the window.
    /// Errors: VideoUnavailable, CreateFailed.
    [[nodiscard]] static auto create(const WindowDesc& desc) -> Result<Window, WindowError>;

    inline Window(Window&& other) noexcept : m_window{std::exchange(other.m_window, nullptr)} {}
    inline auto operator=(Window&& other) noexcept -> Window& {
        if (this != &other) {
            close();
            m_window = std::exchange(other.m_window, nullptr);
        }
        return *this;
    }
    Window(const Window&) = delete;
    auto operator=(const Window&) -> Window& = delete;
    inline ~Window() { close(); }

    /// The size of the drawable area in pixels; zero while minimized.
    [[nodiscard]] auto pixelSize() const -> PixelSize;

    /// The handles a GPU backend makes its surface from. Errors: NoNativeWindow.
    [[nodiscard]] auto nativeWindow() const -> Result<NativeWindow, WindowError>;

    /// Hides the cursor, keeps it in the window and reports raw motion (MouseMoved), or gives the
    /// cursor back. Errors: RelativeMouseUnavailable.
    [[nodiscard]] auto setRelativeMouse(bool isRelative) -> Result<void, WindowError>;

    /// Appends the events that arrived since the last call, in order, to `events`.
    void pollEvents(std::vector<WindowEvent>& events);

private:
    inline explicit Window(void* window) noexcept : m_window{window} {}
    void close() noexcept;

    void* m_window = nullptr; // the SDL_Window
};

} // namespace nx::platform
