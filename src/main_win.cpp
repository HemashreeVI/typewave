// TypeWave for Windows: tray app + low-level keyboard hook.
//
// Privacy: the hook only turns each key into a *kind* (normal, space, enter,
// backspace, modifier) and a stereo position, and hands that to the sound
// engine. Nothing about which keys you press is stored, logged or sent.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <filesystem>
#include <string>

#include "audio.hpp"
#include "engine.hpp"
#include "keys.hpp"
#include "settings.hpp"

namespace fs = std::filesystem;
using namespace tw;

namespace {

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT kTrayId = 1;
constexpr int kHotkeyMute = 1;
constexpr wchar_t kAppName[] = L"TypeWave";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

enum MenuId : UINT {
    IdToggle = 100, IdRelease, IdStereo, IdStartup, IdOpenPacks, IdReloadPacks, IdAbout, IdQuit,
    IdVolumeBase = 200,  // + index into kVolumes
    IdPackBase = 300,    // + pack index
};
constexpr int kVolumes[] = {10, 25, 40, 60, 80, 100};

struct App {
    HINSTANCE inst = nullptr;
    HWND hwnd = nullptr;
    HHOOK hook = nullptr;
    HICON icon = nullptr, iconMuted = nullptr;
    UINT taskbarCreated = 0;
    NOTIFYICONDATAW nid{};
    Engine engine;
    AudioOutput audio;
    Settings settings;
    fs::path dataDir, settingsFile, packsDir;
    bool down[256] = {};
} g;

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

fs::path appDataDir() {
    PWSTR path = nullptr;
    fs::path dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path))) dir = fs::path(path) / L"TypeWave";
    CoTaskMemFree(path);
    if (dir.empty()) dir = fs::current_path() / L"TypeWave";
    return dir;
}

// ---------------------------------------------------------------- settings
void syncSettingsFromEngine() {
    g.settings.enabled = g.engine.enabled;
    g.settings.volume = g.engine.volume;
    g.settings.releaseSounds = g.engine.releaseSounds;
    g.settings.stereo = g.engine.stereo;
    const int cur = g.engine.current;
    if (cur >= 0 && cur < static_cast<int>(g.engine.packCount())) g.settings.pack = g.engine.pack(static_cast<size_t>(cur)).name;
    g.settings.save(g.settingsFile);
}

bool startupEnabled() {
    wchar_t buf[MAX_PATH * 2];
    DWORD size = sizeof(buf);
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kAppName, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS;
}

void setStartup(bool on) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
        RegSetValueExW(key, kAppName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                       static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, kAppName);
    }
    RegCloseKey(key);
}

// ---------------------------------------------------------------- tray
void updateTray() {
    const bool on = g.engine.enabled;
    g.nid.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    g.nid.hIcon = on ? g.icon : g.iconMuted;
    std::wstring tip = std::wstring(kAppName) + (on ? L": " : L" (muted): ");
    const int cur = g.engine.current;
    if (cur >= 0 && cur < static_cast<int>(g.engine.packCount())) tip += widen(g.engine.pack(static_cast<size_t>(cur)).name);
    wcsncpy_s(g.nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &g.nid);
}

void addTray() {
    g.nid = {};
    g.nid.cbSize = sizeof(g.nid);
    g.nid.hWnd = g.hwnd;
    g.nid.uID = kTrayId;
    g.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    g.nid.uCallbackMessage = WM_TRAY;
    g.nid.hIcon = g.icon;
    wcsncpy_s(g.nid.szTip, kAppName, _TRUNCATE);
    Shell_NotifyIconW(NIM_ADD, &g.nid);
    g.nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g.nid);
    updateTray();
}

void balloon(const wchar_t* title, const wchar_t* text) {
    g.nid.uFlags = NIF_INFO;
    wcsncpy_s(g.nid.szInfoTitle, title, _TRUNCATE);
    wcsncpy_s(g.nid.szInfo, text, _TRUNCATE);
    g.nid.dwInfoFlags = NIIF_NONE | NIIF_NOSOUND;
    Shell_NotifyIconW(NIM_MODIFY, &g.nid);
}

void preview() {
    // a short phrase of keys so you can hear a pack right away
    g.engine.post({KeyKind::Normal, false, -0.4f});
    g.engine.post({KeyKind::Normal, true, -0.4f});
}

void setEnabled(bool on) {
    g.engine.enabled = on;
    updateTray();
    syncSettingsFromEngine();
    if (on) preview();
}

void reloadPacks() {
    g.audio.stop();  // nothing may play from a pack while the list changes
    const std::string currentName = g.settings.pack;
    g.engine.removeCustomPacks();
    std::string log;
    for (auto& p : loadCustomPacks(g.packsDir, &log)) g.engine.addPack(std::move(p));
    const int idx = g.engine.findPack(currentName);
    g.engine.current = idx >= 0 ? idx : 0;
    if (!g.audio.start(g.engine)) MessageBoxW(g.hwnd, widen(g.audio.error()).c_str(), kAppName, MB_ICONERROR);
    if (!log.empty()) MessageBoxW(g.hwnd, widen("Some sound files were skipped:\n\n" + log).c_str(), kAppName, MB_ICONWARNING);
    updateTray();
}

void openPacksFolder() {
    writePacksReadme(g.packsDir);
    ShellExecuteW(nullptr, L"open", g.packsDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void showAbout() {
    MessageBoxW(g.hwnd,
                L"TypeWave makes any keyboard sound like a mechanical one.\n\n"
                L"Privacy: TypeWave only checks what kind of key you pressed (letter, space, Enter, "
                L"Backspace or modifier) to choose a sound. It never stores, logs or sends which keys "
                L"you press, and it never connects to the internet.\n\n"
                L"Mute or unmute anywhere: Ctrl + Alt + K\n"
                L"Your own sounds: tray menu > Open sound packs folder\n\n"
                L"Built by Hemashree V I.  Audio by miniaudio (public domain).",
                L"About TypeWave", MB_ICONINFORMATION);
}

void showMenu() {
    HMENU menu = CreatePopupMenu();
    HMENU packs = CreatePopupMenu();
    HMENU volume = CreatePopupMenu();
    const int cur = g.engine.current;
    bool addedSeparator = false;
    for (size_t i = 0; i < g.engine.packCount(); ++i) {
        const Pack& p = g.engine.pack(i);
        if (p.custom && !addedSeparator) { AppendMenuW(packs, MF_SEPARATOR, 0, nullptr); addedSeparator = true; }
        const std::wstring label = widen(p.name) + L"\t" + widen(p.custom ? "your pack" : p.blurb);
        AppendMenuW(packs, MF_STRING | (static_cast<int>(i) == cur ? MF_CHECKED : 0),
                    IdPackBase + static_cast<UINT>(i), label.c_str());
    }
    const int vol = static_cast<int>(g.engine.volume * 100.0f + 0.5f);
    for (size_t i = 0; i < sizeof(kVolumes) / sizeof(kVolumes[0]); ++i) {
        const std::wstring label = std::to_wstring(kVolumes[i]) + L"%";
        AppendMenuW(volume, MF_STRING | (vol == kVolumes[i] ? MF_CHECKED : 0), IdVolumeBase + static_cast<UINT>(i), label.c_str());
    }

    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"TypeWave");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (g.engine.enabled ? MF_CHECKED : 0), IdToggle, L"Sounds on\tCtrl+Alt+K");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(packs), L"Sound pack");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(volume), L"Volume");
    AppendMenuW(menu, MF_STRING | (g.engine.releaseSounds ? MF_CHECKED : 0), IdRelease, L"Key release sounds");
    AppendMenuW(menu, MF_STRING | (g.engine.stereo ? MF_CHECKED : 0), IdStereo, L"Stereo (left keys on the left)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IdOpenPacks, L"Open sound packs folder");
    AppendMenuW(menu, MF_STRING, IdReloadPacks, L"Reload sound packs");
    AppendMenuW(menu, MF_STRING | (startupEnabled() ? MF_CHECKED : 0), IdStartup, L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IdAbout, L"About and privacy");
    AppendMenuW(menu, MF_STRING, IdQuit, L"Quit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g.hwnd);  // so the menu closes when you click elsewhere
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g.hwnd, nullptr);
    PostMessageW(g.hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);  // also destroys the sub-menus
}

void onCommand(UINT id) {
    if (id >= IdPackBase && id < IdPackBase + g.engine.packCount()) {
        g.engine.current = static_cast<int>(id - IdPackBase);
        syncSettingsFromEngine();
        updateTray();
        preview();
        return;
    }
    if (id >= IdVolumeBase && id < IdVolumeBase + sizeof(kVolumes) / sizeof(kVolumes[0])) {
        g.engine.volume = kVolumes[id - IdVolumeBase] / 100.0f;
        syncSettingsFromEngine();
        preview();
        return;
    }
    switch (id) {
        case IdToggle: setEnabled(!g.engine.enabled); break;
        case IdRelease: g.engine.releaseSounds = !g.engine.releaseSounds; syncSettingsFromEngine(); break;
        case IdStereo: g.engine.stereo = !g.engine.stereo; syncSettingsFromEngine(); break;
        case IdStartup: setStartup(!startupEnabled()); break;
        case IdOpenPacks: openPacksFolder(); break;
        case IdReloadPacks: reloadPacks(); break;
        case IdAbout: showAbout(); break;
        case IdQuit: DestroyWindow(g.hwnd); break;
        default: break;
    }
}

// ---------------------------------------------------------------- keyboard hook
LRESULT CALLBACK keyboardHook(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION) {
        const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        // ignore keys sent by other programs (macros, remote tools)
        if (!(k->flags & LLKHF_INJECTED) && k->vkCode < 256) {
            const bool isDown = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
            const bool isUp = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;
            bool& held = g.down[k->vkCode];
            // holding a key auto-repeats WM_KEYDOWN; only the first press makes a sound
            if (isDown && !held) {
                held = true;
                g.engine.post({classify(k->vkCode), false, panFor(k->vkCode)});
            } else if (isUp) {
                held = false;
                g.engine.post({classify(k->vkCode), true, panFor(k->vkCode)});
            }
        }
    }
    return CallNextHookEx(g.hook, code, wParam, lParam);
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g.taskbarCreated && msg != 0) {  // Explorer restarted: put the icon back
        addTray();
        return 0;
    }
    switch (msg) {
        case WM_TRAY:
            // NOTIFYICON_VERSION_4: left click = NIN_SELECT, right click = WM_CONTEXTMENU
            switch (LOWORD(lParam)) {
                case NIN_SELECT:
                case NIN_KEYSELECT:
                case WM_CONTEXTMENU:
                    showMenu();
                    break;
            }
            return 0;
        case WM_HOTKEY:
            if (wParam == kHotkeyMute) {
                setEnabled(!g.engine.enabled);
                balloon(kAppName, g.engine.enabled ? L"Sounds on" : L"Sounds muted. Press Ctrl+Alt+K to turn them back on.");
            }
            return 0;
        case WM_COMMAND:
            onCommand(LOWORD(wParam));
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\TypeWave.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"TypeWave is already running. Look for its icon in the system tray (bottom right, near the clock).",
                    kAppName, MB_ICONINFORMATION);
        return 0;
    }
    g.inst = inst;
    g.dataDir = appDataDir();
    g.settingsFile = g.dataDir / L"settings.ini";
    g.packsDir = g.dataDir / L"packs";
    const bool firstRun = !g.settings.load(g.settingsFile);

    // sound packs: built-in styles first, then the user's own
    for (int s = 0; s < kStyleCount; ++s) g.engine.addPack(buildPack(static_cast<Style>(s)));
    for (auto& p : loadCustomPacks(g.packsDir)) g.engine.addPack(std::move(p));
    const int idx = g.engine.findPack(g.settings.pack);
    g.engine.current = idx >= 0 ? idx : g.engine.findPack("Thock");
    g.engine.volume = g.settings.volume;
    g.engine.enabled = g.settings.enabled;
    g.engine.releaseSounds = g.settings.releaseSounds;
    g.engine.stereo = g.settings.stereo;

    if (!g.audio.start(g.engine)) {
        MessageBoxW(nullptr, widen(g.audio.error()).c_str(), kAppName, MB_ICONERROR);
        return 1;
    }

    g.icon = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                           GetSystemMetrics(SM_CYSMICON), 0));
    g.iconMuted = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(2), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                                GetSystemMetrics(SM_CYSMICON), 0));
    if (!g.icon) g.icon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!g.iconMuted) g.iconMuted = g.icon;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = windowProc;
    wc.hInstance = inst;
    wc.hIcon = g.icon;
    wc.lpszClassName = L"TypeWaveWindow";
    RegisterClassExW(&wc);
    g.hwnd = CreateWindowExW(0, wc.lpszClassName, kAppName, 0, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    g.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    addTray();

    g.hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardHook, GetModuleHandleW(nullptr), 0);
    if (!g.hook) {
        MessageBoxW(nullptr, L"Couldn't listen to the keyboard. Try running TypeWave again.", kAppName, MB_ICONERROR);
        return 1;
    }
    if (!RegisterHotKey(g.hwnd, kHotkeyMute, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'K'))
        balloon(kAppName, L"Ctrl+Alt+K is used by another app, so use the tray icon to mute.");

    if (firstRun) {
        g.settings.save(g.settingsFile);
        writePacksReadme(g.packsDir);
        balloon(L"TypeWave is running",
                L"Start typing to hear it. Right-click this icon to change the sound pack or volume. Ctrl+Alt+K mutes.");
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnhookWindowsHookEx(g.hook);
    UnregisterHotKey(g.hwnd, kHotkeyMute);
    Shell_NotifyIconW(NIM_DELETE, &g.nid);
    syncSettingsFromEngine();
    g.audio.stop();
    if (mutex) CloseHandle(mutex);
    return 0;
}
