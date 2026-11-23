// ArmrCallView: a small debugger-style live viewer for the Armr guest
// activity event stream. It tails a probe log (default: newest
// pvz1-run*.log next to the executable), classifies each line into a
// category (JNI / Network / Files / Heap / GL / Status / Other),
// colour-codes it, and supports pause, clear, category toggles, and a
// substring filter. Console self-test: ArmrCallView --classify <log>
// prints a category histogram instead of opening a window.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <commctrl.h>
#include <atomic>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

namespace {

enum class Category : int {
    kJni = 0,
    kNet,
    kFile,
    kHeap,
    kGl,
    kStatus,
    kOther,
    kCount
};

const wchar_t* const kCategoryNames[] = {L"JNI", L"Net", L"File", L"Heap",
                                         L"GL",  L"Stat", L"Other"};
const char* const kCategoryNamesAscii[] = {"JNI",  "Net",  "File",
                                           "Heap", "GL",   "Stat",
                                           "Other"};
const COLORREF kCategoryColors[] = {
    RGB(46, 160, 67),    // JNI   green
    RGB(224, 130, 20),   // Net   orange
    RGB(0, 150, 150),    // File  teal
    RGB(170, 90, 220),   // Heap  purple
    RGB(70, 130, 240),   // GL    blue
    RGB(224, 60, 60),    // Stat  red
    RGB(160, 160, 160),  // Other gray
};

struct Event {
    std::uint32_t tick = 0;
    std::uint32_t repeats = 1;
    Category category = Category::kOther;
    std::string detail;
};

Category ClassifyLine(const std::string& line, std::string* detail) {
    const std::string_view view(line);
    const auto has = [view](std::string_view needle) {
        return view.find(needle) != std::string_view::npos;
    };
    const auto starts = [view](std::string_view prefix) {
        return view.size() >= prefix.size() &&
               view.compare(0, prefix.size(), prefix) == 0;
    };
    if (starts("ISOMR_EV jni ")) {
        *detail = std::string(view.substr(14));
        return Category::kJni;
    }
    if (starts("ISOMR_EV file ")) {
        *detail = std::string(view.substr(15));
        return Category::kFile;
    }
    if (starts("ISOMR_EV net ")) {
        *detail = std::string(view.substr(14));
        return Category::kNet;
    }
    if (starts("ISOMR_EV gl ")) {
        *detail = std::string(view.substr(13));
        return Category::kGl;
    }
    if (starts("ISOMR_EV heap ")) {
        *detail = std::string(view.substr(15));
        return Category::kHeap;
    }
    if (has("sendGetRequest") || has("DownLoadFile") || has("http://") ||
        has("NetworkConnection")) {
        *detail = line;
        return Category::kNet;
    }
    if (starts("ISOMR_OBB_READ") || has("file-open") ||
        has("FILE_OPEN_FAIL") || starts("ISOMR_EV file")) {
        *detail = line;
        return Category::kFile;
    }
    if (has("BIGMALLOC") || has("BIGALLOC") || has("COPY_FAIL") ||
        has("WWO fail") || has("WCU fail")) {
        *detail = line;
        return Category::kHeap;
    }
    if (has("TEXSUB") || has("glTexImage") || has("glTexSub") ||
        starts("ISOMR_EV gl")) {
        *detail = line;
        return Category::kGl;
    }
    if (has("STALL") || has("FAILURE_SUMMARY") || has("FULL_ERROR") ||
        has("THREAD_END") || has("REAPED") || has("result=fail")) {
        *detail = line;
        return Category::kStatus;
    }
    if (starts("ISOMR_CTR_GETMETHOD") || starts("ISOMR_CTR_GETSTATIC") ||
        has("ISOMR_JNI_") || starts("ISOMR_CTR_VOIDV")) {
        *detail = line;
        return Category::kJni;
    }
    *detail = line;
    return Category::kOther;
}

std::string Narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int needed = WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr,
        0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(needed > 0 ? needed : 0), '\0');
    if (needed > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                            static_cast<int>(text.size()), out.data(),
                            needed, nullptr, nullptr);
    }
    return out;
}

std::wstring Widen(const std::string& text) {
    if (text.empty()) return {};
    const int needed =
        MultiByteToWideChar(CP_UTF8, 0, text.data(),
                            static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(needed > 0 ? needed : 0), L'\0');
    if (needed > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text.data(),
                            static_cast<int>(text.size()), out.data(),
                            needed);
    }
    return out;
}

std::string LowerAscii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

constexpr std::size_t kMaxDetailLength = 600;
constexpr std::size_t kMaxEvents = 120000;

struct AppState {
    HWND window = nullptr;
    HWND list = nullptr;
    HWND filter_edit = nullptr;
    HWND buttons[3] = {};
    HWND checks[static_cast<int>(Category::kCount)] = {};
    std::vector<int> visible;  // indices into events
    std::deque<Event> events;
    std::wstring filter;
    bool category_on[static_cast<int>(Category::kCount)] = {true, true,
                                                            true, true,
                                                            true, true,
                                                            true};
    bool paused = false;
    bool follow = true;
    std::uint32_t start_tick = 0;
};

AppState g_state;
std::atomic<bool> g_tail_running{false};

int RunClassify(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        std::fwprintf(stderr, L"cannot open %ls (error %lu)\n", path.c_str(),
                      GetLastError());
        return 2;
    }
    std::uint64_t counts[static_cast<int>(Category::kCount)] = {};
    std::string pending;
    char buffer[65536];
    for (;;) {
        DWORD read_bytes = 0;
        if (!ReadFile(file, buffer, sizeof(buffer), &read_bytes, nullptr) ||
            read_bytes == 0)
            break;
        pending.append(buffer, read_bytes);
        std::size_t start = 0;
        for (std::size_t bar = pending.find('\n', start); bar != std::string::npos;
             start = bar + 1, bar = pending.find('\n', start)) {
            std::string line = pending.substr(start, bar - start);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            std::string detail;
            ++counts[static_cast<int>(ClassifyLine(line, &detail))];
        }
        pending.erase(0, start);
        if (pending.size() > 4U * 1024U * 1024U) pending.clear();
    }
    CloseHandle(file);
    std::uint64_t total = 0;
    for (std::uint64_t count : counts) total += count;
    std::wprintf(L"classified %llu lines from %ls\n",
                 static_cast<unsigned long long>(total), path.c_str());
    for (int index = 0; index < static_cast<int>(Category::kCount); ++index) {
        std::wprintf(L"  %-6ls %llu\n", kCategoryNames[index],
                     static_cast<unsigned long long>(counts[index]));
    }
    return 0;
}

DWORD WINAPI TailThread(void* parameter) {
    const std::wstring path = *static_cast<std::wstring*>(parameter);
    delete static_cast<std::wstring*>(parameter);
    HANDLE file = INVALID_HANDLE_VALUE;
    std::string pending;
    std::vector<std::string> batch;
    batch.reserve(512);
    const DWORD start = GetTickCount();
    while (g_tail_running.load()) {
        if (file == INVALID_HANDLE_VALUE) {
            file = CreateFileW(path.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE |
                                   FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) {
                Sleep(500);
                continue;
            }
            LARGE_INTEGER size{};
            GetFileSizeEx(file, &size);
            LARGE_INTEGER zero{};
            zero.QuadPart = size.QuadPart;
            SetFilePointerEx(file, zero, nullptr, FILE_BEGIN);
        }
        char buffer[65536];
        DWORD read_bytes = 0;
        if (!ReadFile(file, buffer, sizeof(buffer), &read_bytes, nullptr)) {
            CloseHandle(file);
            file = INVALID_HANDLE_VALUE;
            Sleep(500);
            continue;
        }
        if (read_bytes != 0) {
            pending.append(buffer, read_bytes);
            std::size_t line_start = 0;
            for (std::size_t bar = pending.find('\n', line_start);
                 bar != std::string::npos;
                 line_start = bar + 1,
                           bar = pending.find('\n', line_start)) {
                std::string line =
                    pending.substr(line_start, bar - line_start);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) batch.push_back(std::move(line));
            }
            pending.erase(0, line_start);
            // A single megabyte-scale error dump has no newline for a
            // long time; keep the head so the viewer stays live.
            if (pending.size() > 1024U * 1024U) {
                pending.erase(0, pending.size() - 65536);
            }
        }
        const bool timeout =
            GetTickCount() - start > 0;  // batch flush on every wake
        if (!batch.empty() && (batch.size() >= 512 || timeout)) {
            auto* posted = new std::vector<std::string>(std::move(batch));
            batch.clear();
            if (!PostMessage(g_state.window, WM_APP + 1, 0,
                             reinterpret_cast<LPARAM>(posted))) {
                delete posted;
            }
        }
        if (read_bytes == 0) Sleep(150);
    }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return 0;
}

void RebuildVisible() {
    g_state.visible.clear();
    g_state.visible.reserve(g_state.events.size());
    const std::string needle = LowerAscii(Narrow(g_state.filter));
    int index = 0;
    for (const Event& event : g_state.events) {
        const bool category_ok =
            g_state.category_on[static_cast<int>(event.category)];
        const bool filter_ok =
            needle.empty() ||
            LowerAscii(event.detail).find(needle) != std::string::npos;
        if (category_ok && filter_ok) g_state.visible.push_back(index);
        ++index;
    }
    if (g_state.list != nullptr) {
        ListView_SetItemCountEx(g_state.list,
                                static_cast<int>(g_state.visible.size()),
                                LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
    }
}

bool WasAtBottom() {
    if (g_state.list == nullptr) return true;
    const int count = ListView_GetItemCount(g_state.list);
    if (count <= 0) return true;
    const int top = ListView_GetTopIndex(g_state.list);
    const int page = ListView_GetCountPerPage(g_state.list);
    return top + page >= count - 1;
}

void AppendLine(std::string line) {
    if (line.size() > kMaxDetailLength) {
        line.resize(kMaxDetailLength);
        line += " ...[truncated]";
    }
    std::string detail;
    const Category category = ClassifyLine(line, &detail);
    if (!g_state.events.empty()) {
        Event& last = g_state.events.back();
        if (last.category == category && last.detail == detail) {
            ++last.repeats;
            return;
        }
    }
    Event event;
    event.tick = GetTickCount() - g_state.start_tick;
    event.category = category;
    event.detail = std::move(detail);
    g_state.events.push_back(std::move(event));
    if (g_state.events.size() > kMaxEvents) {
        for (std::size_t i = 0; i < kMaxEvents / 4; ++i)
            g_state.events.pop_front();
        RebuildVisible();
    }
}

void ApplyFilterText() {
    wchar_t text[256] = {};
    GetWindowTextW(g_state.filter_edit, text, 256);
    g_state.filter = text;
    RebuildVisible();
}

LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wparam,
                         LPARAM lparam) {
    switch (message) {
        case WM_CREATE: {
            g_state.window = window;
            g_state.start_tick = GetTickCount();
            const HFONT font =
                static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            const wchar_t* const button_labels[] = {L"Pause", L"Clear",
                                                    L"Open..."};
            for (int i = 0; i < 3; ++i) {
                g_state.buttons[i] = CreateWindowW(
                    L"BUTTON", button_labels[i],
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 8 + i * 78, 6, 72,
                    24, window, reinterpret_cast<HMENU>(
                                    static_cast<INT_PTR>(200 + i)),
                    nullptr, nullptr);
                SendMessageW(g_state.buttons[i], WM_SETFONT,
                             reinterpret_cast<WPARAM>(font), TRUE);
            }
            for (int i = 0; i < static_cast<int>(Category::kCount); ++i) {
                g_state.checks[i] = CreateWindowW(
                    L"BUTTON", kCategoryNames[i],
                    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 250 + i * 62, 8,
                    58, 20, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(
                                          300 + i)),
                    nullptr, nullptr);
                SendMessageW(g_state.checks[i], WM_SETFONT,
                             reinterpret_cast<WPARAM>(font), TRUE);
                SendMessageW(g_state.checks[i], BM_SETCHECK, BST_CHECKED, 0);
            }
            g_state.filter_edit = CreateWindowW(
                L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 700, 8,
                240, 22, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(400)), nullptr,
                nullptr);
            SendMessageW(g_state.filter_edit, WM_SETFONT,
                         reinterpret_cast<WPARAM>(font), TRUE);
            g_state.list = CreateWindowExW(
                0, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA |
                    LVS_SHOWSELALWAYS | WS_VSCROLL | WS_HSCROLL,
                0, 36, 0, 0, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(100)), nullptr,
                nullptr);
            ListView_SetExtendedListViewStyle(
                g_state.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                  LVS_EX_AUTOSIZECOLUMNS);
            LVCOLUMNW columns[3] = {};
            columns[0].mask = LVCF_TEXT | LVCF_WIDTH;
            columns[0].pszText = const_cast<LPWSTR>(L"ms");
            columns[0].cx = 70;
            columns[1].mask = LVCF_TEXT | LVCF_WIDTH;
            columns[1].pszText = const_cast<LPWSTR>(L"n");
            columns[1].cx = 44;
            columns[2].mask = LVCF_TEXT | LVCF_WIDTH;
            columns[2].pszText = const_cast<LPWSTR>(L"event");
            columns[2].cx = 940;
            for (int i = 0; i < 3; ++i)
                ListView_InsertColumn(g_state.list, i, &columns[i]);
            return 0;
        }
        case WM_SIZE: {
            const int width = LOWORD(lparam);
            const int height = HIWORD(lparam);
            MoveWindow(g_state.list, 0, 36, width, height - 36 - 22, TRUE);
            return 0;
        }
        case WM_APP + 1: {
            if (g_state.paused) {
                delete reinterpret_cast<std::vector<std::string>*>(lparam);
                return 0;
            }
            const bool was_bottom = WasAtBottom();
            auto* batch = reinterpret_cast<std::vector<std::string>*>(lparam);
            for (std::string& line : *batch) AppendLine(std::move(line));
            delete batch;
            RebuildVisible();
            if (was_bottom && !g_state.visible.empty()) {
                ListView_EnsureVisible(
                    g_state.list,
                    static_cast<int>(g_state.visible.size()) - 1, FALSE);
            }
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            const int code = HIWORD(wparam);
            if (id >= 200 && id <= 202) {
                if (id == 200) g_state.paused = !g_state.paused;
                if (id == 201) {
                    g_state.events.clear();
                    RebuildVisible();
                }
                if (id == 202) {
                    wchar_t path[MAX_PATH] = {};
                    OPENFILENAMEW open{};
                    open.lStructSize = sizeof(open);
                    open.hwndOwner = window;
                    open.lpstrFilter = const_cast<LPWSTR>(L"Logs\0*.log\0All\0*.*\0");
                    open.lpstrFile = path;
                    open.nMaxFile = MAX_PATH;
                    if (GetOpenFileNameW(&open)) {
                        g_tail_running.store(false);
                        Sleep(50);
                        g_state.events.clear();
                        RebuildVisible();
                        g_tail_running.store(true);
                        auto* tail_path = new std::wstring(path);
                        CreateThread(nullptr, 0, TailThread, tail_path, 0,
                                     nullptr);
                    }
                }
            } else if (id >= 300 && id < 300 + static_cast<int>(Category::kCount)) {
                const int category = id - 300;
                g_state.category_on[category] =
                    SendMessageW(g_state.checks[category], BM_GETCHECK, 0,
                                 0) == BST_CHECKED;
                RebuildVisible();
            } else if (id == 400 && code == EN_CHANGE) {
                ApplyFilterText();
            }
            return 0;
        }
        case WM_NOTIFY: {
            const auto* header =
                reinterpret_cast<NMHDR*>(lparam);
            if (header->idFrom == 100 &&
                header->code == LVN_GETDISPINFOW) {
                auto* info = reinterpret_cast<NMLVDISPINFOW*>(lparam);
                const int row = static_cast<int>(info->item.iItem);
                if (row < 0 ||
                    row >= static_cast<int>(g_state.visible.size()))
                    return 0;
                const Event& event =
                    g_state.events[static_cast<std::size_t>(
                        g_state.visible[static_cast<std::size_t>(row)])];
                wchar_t text[128] = {};
                if (info->item.iSubItem == 0) {
                    swprintf_s(text, L"%u.%02u", event.tick / 1000,
                               (event.tick % 1000) / 10);
                } else if (info->item.iSubItem == 1) {
                    swprintf_s(text, L"%u",
                               event.repeats > 1 ? event.repeats : 0);
                } else {
                    std::string combined(
                        kCategoryNamesAscii[static_cast<int>(
                            event.category)]);
                    combined += "  ";
                    combined += event.detail;
                    std::wstring wide = Widen(combined);
                    wcsncpy_s(info->item.pszText, info->item.cchTextMax,
                              wide.c_str(), _TRUNCATE);
                    return 0;
                }
                wcsncpy_s(info->item.pszText, info->item.cchTextMax, text,
                          _TRUNCATE);
                return 0;
            }
            if (header->idFrom == 100 && header->code == NM_CUSTOMDRAW) {
                auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lparam);
                switch (draw->nmcd.dwDrawStage) {
                    case CDDS_PREPAINT:
                        return CDRF_NOTIFYITEMDRAW;
                    case CDDS_ITEMPREPAINT: {
                        const int row =
                            static_cast<int>(draw->nmcd.dwItemSpec);
                        if (row >= 0 &&
                            row < static_cast<int>(g_state.visible.size())) {
                            const Event& event =
                                g_state.events[static_cast<std::size_t>(
                                    g_state.visible[static_cast<std::size_t>(
                                        row)])];
                            draw->clrText =
                                kCategoryColors[static_cast<int>(
                                    event.category)];
                        }
                        return CDRF_DODEFAULT;
                    }
                    default:
                        break;
                }
            }
            break;
        }
        case WM_DESTROY:
            g_tail_running.store(false);
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

std::wstring FindNewestProbeLog() {
    wchar_t directory[MAX_PATH] = {};
    GetCurrentDirectoryW(MAX_PATH, directory);
    std::wstring newest;
    FILETIME best{};
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(L"pvz1-run*.log", &data);
    if (find == INVALID_HANDLE_VALUE) return newest;
    do {
        if (CompareFileTime(&data.ftLastWriteTime, &best) > 0) {
            best = data.ftLastWriteTime;
            newest = data.cFileName;
        }
    } while (FindNextFileW(find, &data) != 0);
    FindClose(find);
    return newest;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR command,
                    int show) {
    // Console self-test mode used by the build verification.
    if (command != nullptr && wcsstr(command, L"--classify") != nullptr) {
        const wchar_t* cursor = wcsstr(command, L"--classify");
        cursor += wcslen(L"--classify");
        while (*cursor == L' ') ++cursor;
        std::wstring path(cursor);
        while (!path.empty() &&
               (path.back() == L' ' || path.back() == L'"'))
            path.pop_back();
        return RunClassify(path);
    }
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&controls);
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = WndProc;
    window_class.hInstance = instance;
    window_class.lpszClassName = L"ArmrCallView";
    window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    window_class.hbrBackground =
        reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&window_class);
    HWND window = CreateWindowExW(
        0, L"ArmrCallView", L"ARMR Call View - guest activity",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1180, 720,
        nullptr, nullptr, instance, nullptr);
    if (window == nullptr) return 3;
    ShowWindow(window, show);
    UpdateWindow(window);
    std::wstring target = FindNewestProbeLog();
    if (target.empty()) target = L"pvz1-run.log";
    g_tail_running.store(true);
    auto* tail_path = new std::wstring(target);
    CreateThread(nullptr, 0, TailThread, tail_path, 0, nullptr);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    g_tail_running.store(false);
    Sleep(50);
    return 0;
}
