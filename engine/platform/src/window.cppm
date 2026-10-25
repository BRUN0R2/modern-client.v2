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
    SizeUnavailable,          ///< The video driver did not tell the size of the window.
    TextInputUnavailable,     ///< Text input could not be turned on or off.
    ShowFailed,               ///< The window could not be shown.
};

/// How the mouse reports itself to the window.
enum class MouseMode : std::uint8_t {
    Absolute, ///< The cursor shows and leaves the window.
    Relative, ///< The cursor hides and stays in the window, which gets its raw motion (MouseMoved).
};

/// Whether the keys type text (TextEntered): while the console or a text entry is used.
enum class TextInput : std::uint8_t {
    Off,
    On,
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
    PixelSize size; ///< The new drawable size.
};

/// The window was minimized, or restored from it: a minimized window has nothing to draw to.
struct Minimized {
    bool isMinimized = false; ///< False when the window was restored.
};

/// The window gained or lost the keyboard focus.
struct FocusChanged {
    bool isFocused = false; ///< False when the focus was lost.
};

/// A key was pressed or released.
struct KeyChanged {
    Key key{};
    bool isDown = false;   ///< False when the key was released.
    bool isRepeat = false; ///< Sent again while held, by the system's key repeat.
};

/// Relative motion in the mouse's own counts, without the system's acceleration while the mouse is
/// relative (Window::setRelativeMouse): what `sensitivity` scales.
struct MouseMoved {
    float dx = 0.0F; ///< Positive to the right.
    float dy = 0.0F; ///< Positive toward the user.
};

/// A mouse button was pressed or released.
struct MouseButtonChanged {
    MouseButton button{};
    bool isDown = false; ///< False when the button was released.
};

/// Wheel notches, fractional on precise wheels: positive away from the user (MWHEELUP).
struct MouseWheelMoved {
    float dy = 0.0F;
};

/// Text the user typed while text input is on (Window::setTextInput), as the keyboard's layout and
/// the system's input method make it: what a text entry inserts. Its keys are reported too.
struct TextEntered {
    std::string text{}; ///< UTF-8.
};

/// Anything that happened to the window or its input, as pollEvents reports it.
using WindowEvent = std::variant<CloseRequested, Resized, Minimized, FocusChanged, KeyChanged, MouseMoved,
                                 MouseButtonChanged, MouseWheelMoved, TextEntered>;

/// A Win32 window: the HINSTANCE and HWND a Vulkan or D3D11 surface is made from.
struct Win32Window {
    void* instance = nullptr; ///< The HINSTANCE.
    void* window = nullptr;   ///< The HWND.
};

/// An X11 window: the Display* and the Window id.
struct XlibWindow {
    void* display = nullptr;  ///< The Display*.
    std::uint64_t window = 0; ///< The Window id.
};

/// A Wayland window: the wl_display* and the wl_surface*.
struct WaylandWindow {
    void* display = nullptr; ///< The wl_display*.
    void* surface = nullptr; ///< The wl_surface*.
};

/// What a GPU backend makes its surface from; the backend creates the surface itself.
using NativeWindow = std::variant<Win32Window, XlibWindow, WaylandWindow>;

/// The drawable area of a window that asks for no other.
inline constexpr PixelSize kDefaultWindowSize{.width = 1280, .height = 720};

/// How to open the window.
struct WindowDesc {
    std::string_view title;              ///< Shown in the title bar.
    PixelSize size = kDefaultWindowSize; ///< Of the drawable area, before the desktop's scaling.
    bool isResizable = true;             ///< Whether the user may resize the window.
    /// Whether it shows as it opens. A program with nothing to draw yet opens it hidden, and shows it
    /// with its first image (Window::show): no empty window while it loads.
    bool isShown = true;
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

    /// The size of the drawable area in pixels; zero while minimized. Errors: SizeUnavailable.
    [[nodiscard]] auto pixelSize() const -> Result<PixelSize, WindowError>;

    /// The handles a GPU backend makes its surface from. Errors: NoNativeWindow.
    [[nodiscard]] auto nativeWindow() const -> Result<NativeWindow, WindowError>;

    /// Shows the window, opened hidden; one shown stays as it is. Errors: ShowFailed.
    [[nodiscard]] auto show() -> Result<void, WindowError>;
    /// Whether the window is shown.
    [[nodiscard]] auto isShown() const noexcept -> bool;

    /// Puts the mouse in `mode`. Errors: RelativeMouseUnavailable.
    [[nodiscard]] auto setMouseMode(MouseMode mode) -> Result<void, WindowError>;

    /// Turns text input on, for typing in the console and the menus' text entries, or off; it starts
    /// off. While it is on, pollEvents reports TextEntered, and the system may show its input method.
    /// Errors: TextInputUnavailable.
    [[nodiscard]] auto setTextInput(TextInput input) -> Result<void, WindowError>;
    /// Whether text input is on.
    [[nodiscard]] auto isTextInputOn() const noexcept -> bool;

    /// Appends the events that arrived since the last call, in order, to `events`.
    void pollEvents(std::vector<WindowEvent>& events);

private:
    inline explicit Window(void* window) noexcept : m_window{window} {}
    void close() noexcept;

    void* m_window = nullptr; // the SDL_Window
};

} // namespace nx::platform
