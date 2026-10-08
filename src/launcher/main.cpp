#include "disc_import.h"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using namespace disruptor::launcher;
namespace {
constexpr UINT progress_message = WM_APP + 1;
constexpr UINT finished_message = WM_APP + 2;
constexpr int browse_id = 100, play_id = 101, cancel_id = 102, help_id = 103;

fs::path executable_directory() {
    std::wstring buffer(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!size || size >= buffer.size()) throw std::runtime_error("Cannot find the launcher's folder.");
    buffer.resize(size);
    return fs::path(buffer).parent_path();
}

void check_package(const fs::path& root, const DiscProfile& profile) {
    for (const auto& name : {fs::path(L"DisruptorRecompiled.exe"), fs::path(profile.game_config),
                             fs::path(L"bios/openbios.bin")}) {
        if (!fs::is_regular_file(root / name))
            throw std::runtime_error("The build is incomplete. Use Extract All on the release ZIP and keep "
                                     "the launcher, game executable, game.toml, and bios folder together.");
    }
    if (fs::file_size(root / "bios/openbios.bin") != 524288)
        throw std::runtime_error("The bundled OpenBIOS is incomplete. Extract the complete release again.");
}

// Windows command-line quoting, including trailing backslashes and quotes.
std::wstring quote(const std::wstring& argument) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const auto ch : argument) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'\"') result.append(slashes * 2 + 1, L'\\');
        else result.append(slashes, L'\\');
        result += ch;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}

HANDLE start_game(const fs::path& root, const VerifiedDisc& disc) {
    check_package(root, *disc.profile);
    const auto executable = root / "DisruptorRecompiled.exe";
    // Keep disc/config arguments relative and ASCII for the game's path loader.
    std::wstring command = quote(executable.wstring()) + L" --no-launcher --game " +
        quote(disc.profile->game_config) + L" --disc " + quote(disc.cue.lexically_relative(root).wstring());
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE log = CreateFileW((root / "startup.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                             &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) {
        if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
        if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
        throw std::runtime_error("Cannot create startup.log. Put the extracted build in a writable Games folder.");
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = startup.hStdError = log;
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
                                        TRUE, CREATE_NO_WINDOW, nullptr, root.c_str(), &startup, &process);
    const DWORD failure = GetLastError();
    CloseHandle(log);
    CloseHandle(input);
    if (!started)
        throw std::runtime_error("Windows could not start DisruptorRecompiled.exe (error " +
            std::to_string(failure) + "). Check that the complete build is extracted and the "
            "Microsoft Visual C++ x64 Redistributable is installed. See GETTING_STARTED.md.");
    CloseHandle(process.hThread);
    return process.hProcess;
}

struct Update { std::wstring status; unsigned percent; };
struct Result { VerifiedDisc disc; std::wstring error; bool play = false; };

struct App {
    fs::path root;
    HWND window = nullptr, status = nullptr, progress = nullptr;
    HWND browse = nullptr, play = nullptr, cancel = nullptr, auto_play = nullptr;
    HFONT body_font = nullptr, heading_font = nullptr;
    std::thread worker;
    std::atomic_bool cancelled{false};
    bool busy = false, closing = false;
    HANDLE game = nullptr;
    VerifiedDisc verified;
    ~App() {
        cancelled = true;
        if (worker.joinable()) worker.join();
        if (game) CloseHandle(game);
        if (body_font) DeleteObject(body_font);
        if (heading_font) DeleteObject(heading_font);
    }
    void controls() {
        EnableWindow(browse, !busy && !game);
        EnableWindow(play, !busy && !game && verified.profile);
        EnableWindow(cancel, busy && !closing);
        EnableWindow(auto_play, !busy && !game);
    }
    void begin(fs::path source, bool importing, bool launch) {
        if (worker.joinable()) worker.join();
        verified = {}; // release previous read locks before replacing data
        cancelled = false;
        busy = true;
        controls();
        SendMessageW(progress, PBM_SETPOS, 0, 0);
        SetWindowTextW(status, importing ? L"Preparing to import your image..." : L"Checking installed game data...");
        worker = std::thread([this, source = std::move(source), importing, launch] {
            auto result = std::make_unique<Result>();
            result->play = launch;
            try {
                unsigned previous = 101;
                std::wstring previous_status;
                auto report = [this, &previous, &previous_status](const std::wstring& text, unsigned percent) {
                    if (previous == percent && previous_status == text) return;
                    previous = percent;
                    previous_status = text;
                    auto update = std::make_unique<Update>(Update{text, percent});
                    if (PostMessageW(window, progress_message, 0, reinterpret_cast<LPARAM>(update.get())))
                        update.release();
                };
                result->disc = importing ? import_disc(source, root, cancelled, report)
                                         : verify_disc(source, cancelled, report);
                check_package(root, *result->disc.profile);
                if (cancelled) {
                    result->disc = {};
                    result->error = L"Cancelled. Any completed import is kept; reopen the launcher to verify it again.";
                }
            } catch (const std::exception& error) { result->error = error_text(error); }
            if (PostMessageW(window, finished_message, 0, reinterpret_cast<LPARAM>(result.get())))
                result.release();
        });
    }
    void launch() {
        try {
            game = start_game(root, verified);
            SetWindowTextW(status, L"Disruptor is running. You can close this launcher and keep playing.");
            SetTimer(window, 1, 500, nullptr);
        } catch (const std::exception& error) {
            SetWindowTextW(status, error_text(error).c_str());
        }
        controls();
    }
};

HWND control(App& app, const wchar_t* type, const wchar_t* text, DWORD style,
             int x, int y, int width, int height, int id = 0, bool heading = false) {
    const int dpi = static_cast<int>(GetDpiForWindow(app.window));
    const auto scale = [dpi](int value) { return MulDiv(value, dpi, 96); };
    HWND handle = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style,
        scale(x), scale(y), scale(width), scale(height), app.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(heading ? app.heading_font : app.body_font), TRUE);
    return handle;
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    case WM_CREATE: {
        const auto dpi = GetDpiForWindow(window);
        app->body_font = CreateFontW(-MulDiv(11, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
            FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        app->heading_font = CreateFontW(-MulDiv(23, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
            FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        control(*app, L"STATIC", L"Disruptor Recompiled", SS_LEFT, 28, 24, 560, 44, 0, true);
        control(*app, L"STATIC", L"Select your disc image once. The launcher copies and verifies it,\nthen starts the game. Your original files are kept.", SS_LEFT, 30, 80, 560, 50);
        control(*app, L"STATIC", L"Supported: USA / SLUS-00224 \x2022 Raw BIN, CUE, IMG or ISO\nCooked ISO, PAL and Japanese images are not supported yet.", SS_LEFT, 30, 144, 560, 50);
        app->status = control(*app, L"STATIC", L"No game data installed. Choose your disc image to get started.", SS_LEFT, 30, 208, 560, 82);
        app->progress = control(*app, PROGRESS_CLASSW, L"", 0, 30, 300, 560, 18);
        app->auto_play = control(*app, L"BUTTON", L"Start the game after importing", BS_AUTOCHECKBOX | WS_TABSTOP, 30, 330, 350, 28);
        SendMessageW(app->auto_play, BM_SETCHECK, BST_CHECKED, 0);
        app->browse = control(*app, L"BUTTON", L"Browse disc image...", BS_PUSHBUTTON | WS_TABSTOP, 30, 376, 180, 38, browse_id);
        app->play = control(*app, L"BUTTON", L"Play game", BS_DEFPUSHBUTTON | WS_TABSTOP, 224, 376, 130, 38, play_id);
        app->cancel = control(*app, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 368, 376, 100, 38, cancel_id);
        control(*app, L"BUTTON", L"Help", BS_PUSHBUTTON | WS_TABSTOP, 482, 376, 108, 38, help_id);
        app->controls();
        try {
            auto installed = find_installed_disc(app->root);
            if (!installed.empty()) {
                bool legacy = installed != installed_cue(app->root, supported_discs().front());
                app->begin(installed, legacy, false);
            }
        } catch (const std::exception& error) { SetWindowTextW(app->status, error_text(error).c_str()); }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == browse_id && !app->busy && !app->game) {
            std::wstring selected(32768, L'\0');
            OPENFILENAMEW picker{};
            picker.lStructSize = sizeof(picker);
            picker.hwndOwner = window;
            picker.lpstrTitle = L"Select your Disruptor disc image";
            picker.lpstrFilter = L"Disc images (*.cue;*.bin;*.iso;*.img)\0*.cue;*.bin;*.iso;*.img\0CUE sheets (*.cue)\0*.cue\0Raw images (*.bin;*.iso;*.img)\0*.bin;*.iso;*.img\0\0";
            picker.lpstrFile = selected.data();
            picker.nMaxFile = static_cast<DWORD>(selected.size());
            picker.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
            if (GetOpenFileNameW(&picker)) {
                selected.resize(wcslen(selected.c_str()));
                app->begin(fs::path(selected), true, SendMessageW(app->auto_play, BM_GETCHECK, 0, 0) == BST_CHECKED);
            }
        } else if (LOWORD(wparam) == play_id && !app->busy && !app->game && app->verified.profile) {
            const auto cue = app->verified.cue;
            app->begin(cue, false, true); // always verify again immediately before Play
        } else if (LOWORD(wparam) == cancel_id && app->busy) {
            app->cancelled = true;
            SetWindowTextW(app->status, L"Cancelling...");
        } else if (LOWORD(wparam) == help_id) {
            // Windows players may have no Markdown file association.
            const auto guide = quote((app->root / "GETTING_STARTED.md").wstring());
            const auto opened = ShellExecuteW(window, L"open", L"notepad.exe", guide.c_str(), app->root.c_str(), SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(opened) <= 32)
                SetWindowTextW(app->status, L"Open GETTING_STARTED.md in the build folder for setup and troubleshooting help.");
        }
        return 0;
    case progress_message: {
        std::unique_ptr<Update> update(reinterpret_cast<Update*>(lparam));
        if (!app->cancelled) SetWindowTextW(app->status, update->status.c_str());
        SendMessageW(app->progress, PBM_SETPOS, update->percent, 0);
        return 0;
    }
    case finished_message: {
        std::unique_ptr<Result> result(reinterpret_cast<Result*>(lparam));
        if (app->worker.joinable()) app->worker.join();
        app->busy = false;
        if (app->closing) { DestroyWindow(window); return 0; }
        if (!result->error.empty()) {
            // A cancellation can leave an already committed installation.
            // Browsing again or reopening the launcher checks it afresh.
            app->verified = {};
            SetWindowTextW(app->status, result->error.c_str());
            SendMessageW(app->progress, PBM_SETPOS, 0, 0);
        } else {
            app->verified = std::move(result->disc);
            SetWindowTextW(app->status, (std::wstring(L"Verified: ") + app->verified.profile->label +
                L"\nGame data is installed and ready. Select Play game to start.").c_str());
            if (result->play && !app->cancelled) app->launch();
        }
        app->controls();
        return 0;
    }
    case WM_TIMER:
        if (app->game) {
            DWORD code = STILL_ACTIVE;
            if (GetExitCodeProcess(app->game, &code) && code != STILL_ACTIVE) {
                KillTimer(window, 1);
                CloseHandle(app->game);
                app->game = nullptr;
                if (code == 0) SetWindowTextW(app->status, L"Game closed. Select Play game to start again.");
                else SetWindowTextW(app->status,
                    L"The game exited with an error. See startup.log and GETTING_STARTED.md.\nIf a runtime DLL is missing, install the Visual C++ x64 Redistributable.");
                app->controls();
            }
        }
        return 0;
    case WM_CLOSE:
        if (app->busy) {
            app->closing = true;
            app->cancelled = true;
            SetWindowTextW(app->status, L"Finishing cancellation...");
            app->controls();
        } else DestroyWindow(window);
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

// These diagnostic modes use the same production checks as the GUI. They work
// with redirected stdout/stderr without opening a console window.
int diagnostic(int count, wchar_t** arguments) {
    if (count < 2) return -1;
    const std::wstring mode = arguments[1];
    if (mode != L"--verify" && mode != L"--import" && mode != L"--check")
        throw std::runtime_error("Usage: DisruptorLauncher.exe --verify IMAGE | --import IMAGE [--root FOLDER] | --check [--root FOLDER]");
    const bool needs_image = mode != L"--check";
    if (needs_image && count < 3) throw std::runtime_error("A disc image path is required.");
    fs::path root = executable_directory();
    const int option = needs_image ? 3 : 2;
    if (count == option + 2 && std::wstring(arguments[option]) == L"--root") root = fs::absolute(arguments[option + 1]);
    else if (count != option) throw std::runtime_error("Unexpected launcher arguments.");
    std::atomic_bool cancelled{false};
    VerifiedDisc verified;
    if (mode == L"--verify") verified = verify_disc(arguments[2], cancelled);
    else if (mode == L"--import") verified = import_disc(arguments[2], root, cancelled);
    else {
        const auto installed = find_installed_disc(root);
        if (installed.empty()) throw std::runtime_error("No supported disc image is installed.");
        verified = verify_disc(installed, cancelled);
        check_package(root, *verified.profile);
    }
    const std::string message = std::string("Verified ") + verified.profile->id + "\n";
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message.data(), static_cast<DWORD>(message.size()), &written, nullptr);
    return 0;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    try {
        if (!arguments) throw std::runtime_error("Cannot read launcher arguments.");
        int result = diagnostic(count, arguments);
        LocalFree(arguments);
        arguments = nullptr;
        if (result >= 0) return result;
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS};
        InitCommonControlsEx(&controls);
        App app;
        app.root = executable_directory();
        WNDCLASSW cls{};
        cls.lpfnWndProc = window_proc;
        cls.hInstance = instance;
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        cls.lpszClassName = L"DisruptorLauncher";
        if (!RegisterClassW(&cls)) throw std::runtime_error("Cannot create the launcher window.");
        const auto dpi = GetDpiForSystem();
        RECT bounds{0, 0, MulDiv(620, dpi, 96), MulDiv(442, dpi, 96)};
        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        AdjustWindowRectExForDpi(&bounds, style, FALSE, 0, dpi);
        HWND window = CreateWindowExW(0, cls.lpszClassName, L"Disruptor Launcher", style,
            CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
            nullptr, nullptr, instance, &app);
        if (!window) throw std::runtime_error("Cannot create the launcher window.");
        ShowWindow(window, show);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(window, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        return 0;
    } catch (const std::exception& error) {
        if (count > 1) {
            DWORD written = 0;
            const std::string text = std::string(error.what()) + "\n";
            WriteFile(GetStdHandle(STD_ERROR_HANDLE), text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
        } else MessageBoxW(nullptr, error_text(error).c_str(), L"Disruptor Launcher", MB_OK | MB_ICONERROR);
        if (arguments) LocalFree(arguments);
        return 1;
    }
}
