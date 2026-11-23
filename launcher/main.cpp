// ArmrLauncher — native Dear ImGui launcher for the Armr runtime.
//
// Enumerates games/*.profile manifests, renders one card per game, launches
// the profile's probe as `<exe> --game <id> ...`, and streams the child's
// stdout/stderr into a realtime log view inside the same window.
//
// Replaces the WPF Armr-Launcher.ps1: the log is no longer a TextBox pumped by
// PowerShell events; two reader threads drain the child pipes directly into a
// bounded ring buffer rendered every frame, so output appears as it is emitted.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <atomic>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

namespace {

// ---------------------------------------------------------------- D3D11 ----

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swapchain = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
UINT g_resize_w = 0, g_resize_h = 0;

bool CreateDeviceD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0,
                                        D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got{};
    if (D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2,
            D3D11_SDK_VERSION, &sd, &g_swapchain, &g_device, &got,
            &g_context) != S_OK)
        return false;
    ID3D11Texture2D* back = nullptr;
    g_swapchain->GetBuffer(0, IID_PPV_ARGS(&back));
    g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
    return true;
}

void CleanupDeviceD3D() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    if (g_swapchain) { g_swapchain->Release(); g_swapchain = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

void ResizeSwapchain() {
    if (g_resize_w == 0 || g_resize_h == 0) return;
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    g_swapchain->ResizeBuffers(0, g_resize_w, g_resize_h,
                               DXGI_FORMAT_UNKNOWN, 0);
    g_resize_w = g_resize_h = 0;
    ID3D11Texture2D* back = nullptr;
    g_swapchain->GetBuffer(0, IID_PPV_ARGS(&back));
    g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
}

// ------------------------------------------------------------- profiles ----

struct GameProfile {
    std::string id;
    std::string name;
    std::string desc;
    std::string driver;
    std::string exe = "ArmrCtrEntryProbe.exe";
    // Optional full argument template for probes that do not use the generic
    // `--game <id>` form (e.g. Candy's positional ArmrCandyEntryProbe). When
    // non-empty it is authoritative; placeholders {id} {name} {rootlib}
    // {payload} {data} {repo} {build} {localappdata} are expanded at launch.
    std::string args;
    std::string payload_dir;
    std::string data_dir;
    std::string root_lib;
    std::uint64_t render_timeout = 600;
    bool staged = false;
    // Resolved absolute host paths (placeholder-expanded), filled at load.
    std::wstring payload_abs, data_abs, rootlib_abs;
};

std::string Trim(std::string v) {
    const auto ws = [](unsigned char c) { return std::isspace(c) != 0; };
    v.erase(v.begin(),
            std::find_if(v.begin(), v.end(),
                         [&](unsigned char c) { return !ws(c); }));
    v.erase(std::find_if(v.rbegin(), v.rend(),
                         [&](unsigned char c) { return !ws(c); })
                .base(),
            v.end());
    return v;
}

std::wstring ToWide(const std::string& s) {
    std::wstring w;
    w.reserve(s.size());
    for (char c : s) w.push_back(static_cast<wchar_t>(c));
    return w;
}

bool PathExists(const std::wstring& p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Walk up from the executable directory then the cwd looking for `games/`.
// Returns the repository root with a trailing backslash, or empty.
std::wstring FindRepoRoot() {
    wchar_t exe_dir[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe_dir, MAX_PATH);
    wchar_t cwd[MAX_PATH] = {};
    GetCurrentDirectoryW(MAX_PATH, cwd);
    for (int start = 0; start < 2; ++start) {
        std::wstring dir =
            start == 0 ? std::wstring(exe_dir) : std::wstring(cwd);
        for (;;) {
            const std::size_t slash = dir.find_last_of(L"\\/");
            if (slash == std::wstring::npos) break;
            dir = dir.substr(0, slash);
            while (dir.size() > 3 && dir.back() == L'\\') dir.pop_back();
            if (dir.size() > 3 && PathExists(dir + L"\\games"))
                return dir + L"\\";
            if (dir.size() <= 3) break;
        }
    }
    return std::wstring();
}

// ---- placeholder + path resolution for profiles ----

std::wstring GetLocalAppData() {
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    return n ? std::wstring(buf, n) : std::wstring();
}

std::wstring BuildDir(const std::wstring& repo) {
    return repo + L"native-runtime\\win\\build\\aarch64\\";
}

void ReplaceAll(std::wstring& s, const wchar_t* key, const std::wstring& val) {
    std::wstring k = key;
    for (std::size_t pos = 0; (pos = s.find(k, pos)) != std::wstring::npos;
         pos += val.size())
        s.replace(pos, k.size(), val);
}

// Expand {localappdata}/{repo}/{build} then make the result absolute against
// the repo root if it is still relative. Returns a normalized (backslash) path.
std::wstring ResolveDir(std::wstring p, const std::wstring& repo) {
    ReplaceAll(p, L"{localappdata}", GetLocalAppData());
    ReplaceAll(p, L"{repo}", repo);
    ReplaceAll(p, L"{build}", BuildDir(repo));
    std::replace(p.begin(), p.end(), L'/', L'\\');
    bool abs = p.find(L':') != std::wstring::npos || p.rfind(L"\\\\", 0) == 0;
    if (!abs) {
        while (!p.empty() && p.front() == L'\\') p.erase(p.begin());
        p = repo + p;
    }
    return p;
}

std::wstring QuoteIfSpaced(const std::wstring& p) {
    return p.find(L' ') != std::wstring::npos ? (L"\"" + p + L"\"") : p;
}

// Expand every placeholder in a profile `args` template using the resolved
// absolute paths. {rootlib}/{payload}/{data} are quoted when they contain
// spaces so the child parses them as single argv entries.
std::wstring ExpandArgs(std::wstring s, const GameProfile& g,
                        const std::wstring& repo) {
    ReplaceAll(s, L"{rootlib}", QuoteIfSpaced(g.rootlib_abs));
    ReplaceAll(s, L"{payload}", QuoteIfSpaced(g.payload_abs));
    ReplaceAll(s, L"{data}", QuoteIfSpaced(g.data_abs));
    ReplaceAll(s, L"{id}", ToWide(g.id));
    ReplaceAll(s, L"{name}", ToWide(g.name));
    ReplaceAll(s, L"{localappdata}", GetLocalAppData());
    ReplaceAll(s, L"{build}", BuildDir(repo));
    ReplaceAll(s, L"{repo}", repo);
    return s;
}

std::vector<GameProfile> LoadProfiles(const std::wstring& repo) {
    std::vector<GameProfile> games;
    const std::wstring games_dir = repo + L"games\\";
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW((games_dir + L"*.profile").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return games;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::ifstream in(games_dir + fd.cFileName);
        GameProfile g;
        std::string line;
        while (std::getline(in, line)) {
            const auto hash = line.find('#');
            if (hash != std::string::npos) line.erase(hash);
            const std::string t = Trim(line);
            const auto eq = t.find('=');
            if (t.empty() || eq == std::string::npos) continue;
            const std::string k = Trim(t.substr(0, eq));
            const std::string v = Trim(t.substr(eq + 1));
            if (k == "id") g.id = v;
            else if (k == "name") g.name = v;
            else if (k == "desc") g.desc = v;
            else if (k == "driver") g.driver = v;
            else if (k == "exe") g.exe = v;
            else if (k == "args") g.args = v;
            else if (k == "payload_dir") g.payload_dir = v;
            else if (k == "data_dir") g.data_dir = v;
            else if (k == "root_lib") g.root_lib = v;
            else if (k == "render_timeout") {
                try { g.render_timeout = std::stoull(v); }
                catch (...) {}
            }
        }
        if (g.id.empty() || g.payload_dir.empty() || g.root_lib.empty())
            continue;
        if (g.name.empty()) g.name = g.id;
        // Resolve payload/data/root-lib to absolute host paths (expanding
        // {localappdata}/{repo}/{build}); default data dir is a sibling `data`.
        g.payload_abs = ResolveDir(ToWide(g.payload_dir), repo);
        std::wstring data_dir = g.data_dir.empty()
                                    ? ToWide(g.payload_dir) + L"/../data"
                                    : ToWide(g.data_dir);
        g.data_abs = ResolveDir(data_dir, repo);
        std::wstring rl = ToWide(g.root_lib);
        std::replace(rl.begin(), rl.end(), L'/', L'\\');
        g.rootlib_abs = g.payload_abs + L"\\" + rl;
        g.staged = PathExists(g.rootlib_abs);
        games.push_back(std::move(g));
    } while (FindNextFileW(find, &fd) != 0);
    FindClose(find);
    return games;
}

// ------------------------------------------------------------ log buffer ---

struct LogLine {
    std::string text;
    bool err = false;
};

struct LogBuffer {
    void Push(std::string line, bool err) {
        std::lock_guard<std::mutex> lock(mutex_);
        lines_.push_back({std::move(line), err});
        if (lines_.size() > kMax) lines_.pop_front();
        ++version_;
    }
    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        lines_.clear();
    }
    std::uint64_t Version() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return version_;
    }
    template <typename F> void ForEach(F&& fn) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const LogLine& l : lines_) fn(l);
    }

    static constexpr std::size_t kMax = 20000;
    mutable std::mutex mutex_;
    std::deque<LogLine> lines_;
    std::uint64_t version_ = 0;
};

// ----------------------------------------------------------- child proc ----

struct ChildProcess {
    bool Launch(const std::wstring& exe, const std::wstring& args,
                const std::wstring& cwd, LogBuffer* log) {
        if (running_) return false;
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE out_r = nullptr, out_w = nullptr;
        HANDLE err_r = nullptr, err_w = nullptr;
        if (!CreatePipe(&out_r, &out_w, &sa, 0) ||
            !CreatePipe(&err_r, &err_w, &sa, 0))
            return false;
        // Keep the parent's read ends local; only the write ends are inherited.
        SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);

        std::wstring cmd = L"\"" + exe + L"\" " + args;
        std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
        cmd_buf.push_back(L'\0');

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = out_w;
        si.hStdError = err_w;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION pi{};
        const BOOL ok = CreateProcessW(
            exe.c_str(), cmd_buf.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW, nullptr, cwd.c_str(), &si, &pi);
        CloseHandle(out_w);
        CloseHandle(err_w);
        if (!ok) {
            CloseHandle(out_r);
            CloseHandle(err_r);
            return false;
        }
        proc_ = pi.hProcess;
        CloseHandle(pi.hThread);
        running_ = true;
        log_ = log;
        exit_code_ = 0;
        have_exit_ = false;
        t_out_ = std::thread(&ChildProcess::Drain, this, out_r, false);
        t_err_ = std::thread(&ChildProcess::Drain, this, err_r, true);
        return true;
    }

    void Drain(HANDLE pipe, bool err) {
        std::string pending;
        char buf[4096];
        for (;;) {
            DWORD n = 0;
            if (!ReadFile(pipe, buf, sizeof(buf), &n, nullptr) || n == 0)
                break;
            pending.append(buf, n);
            for (;;) {
                const auto nl = pending.find('\n');
                if (nl == std::string::npos) break;
                std::string line = pending.substr(0, nl);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                pending.erase(0, nl + 1);
                log_->Push(std::move(line), err);
            }
        }
        if (!pending.empty()) {
            if (pending.back() == '\r') pending.pop_back();
            log_->Push(std::move(pending), err);
        }
        CloseHandle(pipe);
    }

    void Stop() {
        if (proc_ != nullptr) TerminateProcess(proc_, 1);
    }

    // Reap after exit: waits the readers so their final pushes land.
    void Poll(LogBuffer* log) {
        if (!running_ || proc_ == nullptr) return;
        if (WaitForSingleObject(proc_, 0) != WAIT_OBJECT_0) return;
        GetExitCodeProcess(proc_, &exit_code_);
        have_exit_ = true;
        if (t_out_.joinable()) t_out_.join();
        if (t_err_.joinable()) t_err_.join();
        CloseHandle(proc_);
        proc_ = nullptr;
        running_ = false;
        char tail[80];
        std::snprintf(tail, sizeof(tail),
                      "--- process exited (code %lu) ---",
                      static_cast<unsigned long>(exit_code_));
        log->Push(tail, true);
    }

    ~ChildProcess() {
        Stop();
        if (t_out_.joinable()) t_out_.join();
        if (t_err_.joinable()) t_err_.join();
        if (proc_ != nullptr) CloseHandle(proc_);
    }

    std::atomic<bool> running_{false};
    bool have_exit_ = false;
    DWORD exit_code_ = 0;

private:
    HANDLE proc_ = nullptr;
    LogBuffer* log_ = nullptr;
    std::thread t_out_, t_err_;
};

// ------------------------------------------------------------ win32 glue ---

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
    switch (msg) {
    case WM_SIZE:
        if (g_device != nullptr && wp != SIZE_MINIMIZED) {
            g_resize_w = LOWORD(lp);
            g_resize_h = HIWORD(lp);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

ImVec4 Rgb(std::uint32_t hex) {
    return ImVec4(((hex >> 16) & 0xff) / 255.f, ((hex >> 8) & 0xff) / 255.f,
                  (hex & 0xff) / 255.f, 1.f);
}

void ApplyTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 8.f;
    s.ChildRounding = 8.f;
    s.FrameRounding = 6.f;
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(10, 8);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = Rgb(0x1E1E2E);
    c[ImGuiCol_ChildBg] = Rgb(0x181825);
    c[ImGuiCol_FrameBg] = Rgb(0x313244);
    c[ImGuiCol_FrameBgHovered] = Rgb(0x45475A);
    c[ImGuiCol_FrameBgActive] = Rgb(0x585B70);
    c[ImGuiCol_Button] = Rgb(0x313244);
    c[ImGuiCol_ButtonHovered] = Rgb(0x45475A);
    c[ImGuiCol_ButtonActive] = Rgb(0x585B70);
    c[ImGuiCol_Text] = Rgb(0xCDD6F4);
    c[ImGuiCol_TextDisabled] = Rgb(0x9399B2);
    c[ImGuiCol_Border] = Rgb(0x45475A);
    c[ImGuiCol_Header] = Rgb(0x313244);
    c[ImGuiCol_HeaderHovered] = Rgb(0x45475A);
    c[ImGuiCol_HeaderActive] = Rgb(0x585B70);
    c[ImGuiCol_ScrollbarBg] = Rgb(0x11111B);
    c[ImGuiCol_ScrollbarGrab] = Rgb(0x45475A);
}

}  // namespace

// ------------------------------------------------------------------ main ---

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    // Give spawned probes the relocated-heap fast path by default: both entry
    // probes read ARMR_GUEST_HEAP_BASE and move the guest arena into the
    // direct page-table window (~12x faster startup on pvz1). An explicit
    // value already present in the launcher's environment wins, so the
    // variable remains an override for A/B runs from a shell.
    if (GetEnvironmentVariableW(L"ARMR_GUEST_HEAP_BASE", nullptr, 0) == 0)
        SetEnvironmentVariableW(L"ARMR_GUEST_HEAP_BASE", L"0x40000000");

    WNDCLASSEXW wc{sizeof(wc),
                   CS_CLASSDC,
                   WndProc,
                   0, 0,
                   inst,
                   nullptr, nullptr, nullptr, nullptr,
                   L"ArmrLauncher", nullptr};
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(
        wc.lpszClassName, L"ARMR - native Windows game runtime",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1160, 720,
        nullptr, nullptr, inst, nullptr);
    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, inst);
        return 1;
    }
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ApplyTheme();
    ImFont* ui_font = io.Fonts->AddFontDefault();
    ImFont* mono = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\consola.ttf", 14.0f);
    if (mono == nullptr) mono = ui_font;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    const std::wstring repo = FindRepoRoot();
    std::vector<GameProfile> games =
        repo.empty() ? std::vector<GameProfile>{}
                     : LoadProfiles(repo + L"");
    std::string status =
        repo.empty() ? "games/ directory not found — run from the repo"
                     : "Ready.";
    int selected = games.empty() ? -1 : 0;
    char timeout_buf[32] = {};
    char args_buf[512] = {};
    bool autoscroll = true;
    LogBuffer log;
    ChildProcess child;
    std::uint64_t last_log_version = 0;

    if (!games.empty())
        std::snprintf(timeout_buf, sizeof(timeout_buf), "%llu",
                      static_cast<unsigned long long>(
                          games[0].render_timeout));

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
        ResizeSwapchain();
        child.Poll(&log);

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::GetBackgroundDrawList()->AddRectFilled(
            vp->WorkPos,
            ImVec2(vp->WorkPos.x + vp->WorkSize.x,
                   vp->WorkPos.y + vp->WorkSize.y),
            IM_COL32(0x1E, 0x1E, 0x2E, 0xFF));
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("##root", nullptr,
                     ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoBringToFrontOnFocus |
                         ImGuiWindowFlags_NoSavedSettings);

        // Header
        ImGui::TextColored(Rgb(0xF5C2E7), "ARMR");
        ImGui::SameLine();
        ImGui::TextDisabled("native Windows game runtime");
        ImGui::SameLine();
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x - 1, 0));
        ImGui::Separator();

        const float side_w = 330.f;
        // ------------------------------ left: game cards -----------------
        ImGui::BeginChild("##games", ImVec2(side_w, 0), true);
        ImGui::TextDisabled("GAMES");
        ImGui::Spacing();
        for (int i = 0; i < static_cast<int>(games.size()); ++i) {
            const GameProfile& g = games[i];
            const bool sel = selected == i;
            if (sel) {
                ImGui::PushStyleColor(ImGuiCol_Border, Rgb(0xF5C2E7));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 2.f);
            }
            ImGui::PushID(i);
            if (ImGui::Selectable("##card", sel,
                                  ImGuiSelectableFlags_AllowItemOverlap,
                                  ImVec2(0, 74))) {
                selected = i;
                std::snprintf(timeout_buf, sizeof(timeout_buf), "%llu",
                              static_cast<unsigned long long>(
                                  g.render_timeout));
            }
            ImVec2 p = ImGui::GetItemRectMin();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddText(ImVec2(p.x + 10, p.y + 8), IM_COL32(0xCD, 0xD6, 0xF4, 0xFF),
                        g.name.c_str());
            dl->AddText(ImVec2(p.x + 10, p.y + 30),
                        g.staged ? IM_COL32(0xA6, 0xE3, 0xA1, 0xFF)
                                 : IM_COL32(0xF3, 0x8B, 0xA8, 0xFF),
                        g.staged ? "ready" : "not imported");
            const std::string sub =
                g.desc.empty() ? g.driver : g.desc;
            dl->AddText(ImVec2(p.x + 10, p.y + 50),
                        IM_COL32(0x93, 0x99, 0xB2, 0xFF), sub.c_str());
            ImGui::PopID();
            if (sel) {
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }
        }
        if (games.empty())
            ImGui::TextWrapped("No games found. Drop a manifest under "
                               "games/<id>.profile.");
        ImGui::EndChild();

        ImGui::SameLine();

        // ------------------------- right: options + log ------------------
        ImGui::BeginChild("##main", ImVec2(0, 0), false);

        ImGui::BeginChild("##opts", ImVec2(0, 118), true);
        const GameProfile* g =
            (selected >= 0 && selected < (int)games.size())
                ? &games[selected]
                : nullptr;
        if (g) {
            ImGui::Text("%s", g->name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("id=%s  %s=%s  lib=%s", g->id.c_str(),
                                g->driver.empty() ? "host" : "driver",
                                g->driver.empty() ? g->exe.c_str()
                                                  : g->driver.c_str(),
                                g->root_lib.c_str());
            if (!g->staged) {
                ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.66f, 1.0f),
                                   "Not imported. Run: windows\\Import-ArmrGame.ps1 "
                                   "-Game %s -Package <your .apk/.xapk>",
                                   g->id.c_str());
            }
        } else {
            ImGui::TextDisabled("Select a game.");
        }
        ImGui::SetNextItemWidth(110);
        ImGui::InputText("Render timeout", timeout_buf, sizeof(timeout_buf),
                         ImGuiInputTextFlags_CharsDecimal);
        ImGui::SameLine();
        // Leave room for the label to the right of the field.
        ImGui::SetNextItemWidth(-(ImGui::CalcTextSize("Extra probe args").x +
                                  ImGui::GetStyle().ItemInnerSpacing.x + 4.0f));
        ImGui::InputText("Extra probe args", args_buf, sizeof(args_buf));

        const bool running = child.running_.load();
        ImGui::BeginDisabled(running || g == nullptr || !g->staged);
        if (ImGui::Button("Launch", ImVec2(120, 30))) {
            log.Clear();
            // Re-read the manifest so profile edits (heap, args, mirrors) take
            // effect on the next launch without restarting the launcher;
            // `games` is otherwise the startup snapshot.
            const std::string want_id = g->id;
            games = LoadProfiles(repo + L"");
            g = nullptr;
            selected = -1;
            for (int index = 0; index < (int)games.size(); ++index) {
                if (games[index].id == want_id) {
                    selected = index;
                    g = &games[index];
                    break;
                }
            }
            if (g == nullptr) {
                status = "Profile removed: " + want_id;
                log.Push("ERROR: profile not found after reload: " + want_id,
                         true);
            } else {
                std::wstring exe = repo + L"native-runtime\\win\\build\\aarch64\\" +
                                   ToWide(g->exe);
                std::wstring args;
                if (!g->args.empty()) {
                    // Dedicated-probe profile: the `args` template is authoritative
                    // (e.g. Candy's positional ArmrCandyEntryProbe invocation).
                    args = ExpandArgs(ToWide(g->args), *g, repo);
                } else {
                    args = L"--game " + ToWide(g->id) +
                           L" native-render-timeout=" + ToWide(timeout_buf);
                    if (args_buf[0] != '\0') args += L" " + ToWide(args_buf);
                }
                // The probe mounts the data dir; make sure it exists.
                if (!g->data_abs.empty())
                    CreateDirectoryW(g->data_abs.c_str(), nullptr);
                if (!PathExists(exe)) {
                    status = "Probe not built: " + g->exe;
                    log.Push("ERROR: probe not built: " + g->exe, true);
                } else if (child.Launch(exe, args, repo, &log)) {
                    status = "Running " + g->id + " ...";
                    log.Push("--- launching " + g->id + " via " + g->exe +
                                 " ---",
                             false);
                } else {
                    status = "Launch failed (CreateProcess).";
                    log.Push("ERROR: CreateProcess failed", true);
                }
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!running);
        if (ImGui::Button("Stop", ImVec2(90, 30))) {
            child.Stop();
            status = "Stopping...";
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &autoscroll);
        ImGui::SameLine();
        if (ImGui::Button("Clear log")) log.Clear();
        ImGui::EndChild();

        // ----------------------------- realtime log ----------------------
        ImGui::BeginChild("##log", ImVec2(0, -30), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PushFont(mono);
        const std::uint64_t ver = log.Version();
        const bool follow =
            autoscroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.f;
        log.ForEach([](const LogLine& l) {
            if (l.err)
                ImGui::TextColored(Rgb(0xF38BA8), "%s", l.text.c_str());
            else
                ImGui::TextColored(Rgb(0xA6E3A1), "%s", l.text.c_str());
        });
        if (ver != last_log_version && follow) ImGui::SetScrollHereY(1.f);
        last_log_version = ver;
        ImGui::PopFont();
        ImGui::EndChild();

        // ------------------------------ status bar -----------------------
        ImGui::TextDisabled("%s", status.c_str());
        if (running) {
            ImGui::SameLine();
            ImGui::TextColored(Rgb(0xA6E3A1), "  [running]");
        }
        ImGui::EndChild();
        ImGui::End();

        ImGui::Render();
        const float clear[4] = {0x1E / 255.f, 0x1E / 255.f, 0x2E / 255.f, 1.f};
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swapchain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, inst);
    return 0;
}
