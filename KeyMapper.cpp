// 包含 windows.h 必须在定义 UNICODE 之后
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <wchar.h> // 用于 wcsncpy
#include <string.h> // 用于 memcmp
#include <map>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <mutex>

#define BASE_DIR L"H:\\progrem"

// --- 全局变量和常量 ---
#define WM_TRAY_ICON_MESSAGE (WM_USER + 1)
#define ID_ENABLE_DISABLE 1001
#define ID_QUIT           1002
#define ID_TRAY_ICON      1003
#define ID_TOGGLE_REPEAT  1004
#define ID_TOGGLE_F6Y     1005
#define WM_RELOAD_MAPPINGS (WM_USER + 2)

// 托盘图标数据结构
NOTIFYICONDATA nid; 
bool is_active = true; // 功能激活状态
HHOOK hHook = NULL;    // 键盘钩子句柄

// 用于标记程序自身注入的事件，避免与远程/第三方注入事件混淆
static ULONG_PTR g_inject_marker = 0;

// 按键重复状态跟踪：解决 F6/F7 连续输出问题
bool is_f6_pressed = false;
bool is_f7_pressed = false;

// 配置：按住时是否连续触发映射（默认 false，保持原有触发式行为）
bool repeat_on_hold = false;

// --- 自定义映射数据结构 ---
struct ActionSeq {
    // 已编码为 INPUT 序列（按下/抬起）
    std::vector<INPUT> inputs;
};

struct Mapping {
    DWORD triggerVk = 0;
    DWORD triggerScan = 0;
    std::vector<ActionSeq> actions; // 多个 action 串联执行
    bool is_pressed = false; // runtime state
    bool enabled = true; // mapping enabled
    std::wstring triggerName; // original trigger name for UI
};

static std::vector<Mapping> g_mappings; // 解析后的映射表
static std::mutex g_mappings_mutex;

// Helpers: trim & uppercase
static std::wstring trim(const std::wstring &s) {
    size_t a = 0, b = s.size();
    while (a < b && iswspace(s[a])) ++a;
    while (b > a && iswspace(s[b-1])) --b;
    return s.substr(a, b-a);
}

static std::wstring tolowerwstr(const std::wstring &s) {
    std::wstring r = s;
    for (auto &c : r) c = (wchar_t)towlower(c);
    return r;
}

// Known name -> VK mapping
static bool NameToVk(const std::wstring &name, DWORD &outVk) {
    std::wstring n = tolowerwstr(trim(name));
    if (n.empty()) return false;
    if (n == L"shift") { outVk = VK_SHIFT; return true; }
    if (n == L"ctrl" || n == L"control") { outVk = VK_CONTROL; return true; }
    if (n == L"alt") { outVk = VK_MENU; return true; }
    if (n == L"win" || n == L"lwin" || n == L"rwin") { outVk = VK_LWIN; return true; }
    if (n.size() == 1) {
        // letters and digits
        wchar_t ch = n[0];
        if ((ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9')) {
            SHORT vk = VkKeyScanW(ch);
            outVk = vk & 0xff;
            return true;
        }
    }
    // Function keys F1..F24
    if (n.size() >= 2 && n[0] == L'f') {
        int num = _wtoi(n.c_str()+1);
        if (num >= 1 && num <= 24) { outVk = VK_F1 + (num-1); return true; }
    }
    // common names
    if (n == L"enter" || n == L"return") { outVk = VK_RETURN; return true; }
    if (n == L"space" || n == L" ") { outVk = VK_SPACE; return true; }
    if (n == L"tab") { outVk = VK_TAB; return true; }
    if (n == L"escape" || n == L"esc") { outVk = VK_ESCAPE; return true; }
    if (n == L"backspace") { outVk = VK_BACK; return true; }
    if (n == L"up") { outVk = VK_UP; return true; }
    if (n == L"down") { outVk = VK_DOWN; return true; }
    if (n == L"left") { outVk = VK_LEFT; return true; }
    if (n == L"right") { outVk = VK_RIGHT; return true; }
    if (n == L"home") { outVk = VK_HOME; return true; }
    if (n == L"end") { outVk = VK_END; return true; }
    if (n == L"pageup") { outVk = VK_PRIOR; return true; }
    if (n == L"pagedown") { outVk = VK_NEXT; return true; }
    if (n == L"insert") { outVk = VK_INSERT; return true; }
    if (n == L"delete") { outVk = VK_DELETE; return true; }
    return false;
}

// Build an INPUT sequence for a token like "Shift+6" or "a".
static ActionSeq BuildActionForToken(const std::wstring &token) {
    ActionSeq seq;
    // split by '+'
    std::vector<std::wstring> parts;
    std::wstring cur;
    for (size_t i=0;i<token.size();++i) {
        if (token[i] == L'+') {
            parts.push_back(trim(cur)); cur.clear();
        } else cur.push_back(token[i]);
    }
    if (!cur.empty()) parts.push_back(trim(cur));

    std::vector<DWORD> mods;
    DWORD mainVk = 0;
    for (auto &p : parts) {
        DWORD vk;
        if (NameToVk(p, vk)) {
            // check if modifier
            std::wstring lows = tolowerwstr(p);
            if (lows == L"shift" || lows == L"ctrl" || lows == L"control" || lows == L"alt" || lows == L"win" || lows == L"lwin" || lows == L"rwin") {
                mods.push_back(vk);
            } else {
                mainVk = vk;
            }
        } else {
            // try single char
            if (!p.empty()) {
                SHORT vkres = VkKeyScanW(p[0]);
                mainVk = vkres & 0xff;
            }
        }
    }

    // Build inputs: press mods, press main, release main, release mods
    std::vector<INPUT> tmp;
    for (auto mv : mods) {
        INPUT in = {}; in.type = INPUT_KEYBOARD; in.ki.wScan = (WORD)MapVirtualKeyW(mv, MAPVK_VK_TO_VSC); in.ki.dwFlags = KEYEVENTF_SCANCODE; tmp.push_back(in);
    }
    if (mainVk != 0) {
        INPUT down = {}; down.type = INPUT_KEYBOARD; down.ki.wScan = (WORD)MapVirtualKeyW(mainVk, MAPVK_VK_TO_VSC); down.ki.dwFlags = KEYEVENTF_SCANCODE; tmp.push_back(down);
        INPUT up = {}; up.type = INPUT_KEYBOARD; up.ki.wScan = (WORD)MapVirtualKeyW(mainVk, MAPVK_VK_TO_VSC); up.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP; tmp.push_back(up);
    }
    for (int i = (int)mods.size()-1; i >= 0; --i) {
        INPUT in = {}; in.type = INPUT_KEYBOARD; in.ki.wScan = (WORD)MapVirtualKeyW(mods[i], MAPVK_VK_TO_VSC); in.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP; tmp.push_back(in);
    }

    // 给所有注入的 INPUT 加上特定的 dwExtraInfo 标记，以便钩子能区分我们自己注入的事件
    for (auto &in : tmp) {
        in.ki.dwExtraInfo = g_inject_marker;
    }
    seq.inputs = std::move(tmp);
    return seq;
}

// Load mappings.txt which contains lines like: {[F6]=[y]} or {[a]=[b]+[c]+[Shift+6]}
// parse lines into mappings
static std::vector<Mapping> ParseMappingsFromLines(const std::vector<std::wstring>& lines) {
    std::vector<Mapping> loaded;
    for (auto &line : lines) {
        std::wstring l = trim(line);
        if (l.empty()) continue;
        size_t lbrace = l.find(L'{');
        size_t rbrace = l.find(L'}');
        if (lbrace == std::wstring::npos || rbrace == std::wstring::npos) continue;
        size_t eq = l.find(L'=');
        if (eq == std::wstring::npos) continue;
        size_t left_r = l.rfind(L']', eq);
        size_t left_l = l.rfind(L'[', left_r);
        if (left_l == std::wstring::npos || left_r == std::wstring::npos) continue;
        std::wstring left = l.substr(left_l+1, left_r-left_l-1);
        std::vector<std::wstring> actions;
        size_t pos = eq+1;
        while (true) {
            size_t a = l.find(L'[', pos);
            if (a == std::wstring::npos) break;
            size_t b = l.find(L']', a);
            if (b == std::wstring::npos) break;
            actions.push_back(l.substr(a+1, b-a-1));
            pos = b+1;
        }
        if (actions.empty()) continue;
        Mapping m;
        m.triggerName = left;
        DWORD vk;
        if (NameToVk(left, vk)) {
            m.triggerVk = vk;
            m.triggerScan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        } else if (!left.empty()) {
            SHORT vks = VkKeyScanW(left[0]); m.triggerVk = vks & 0xff; m.triggerScan = MapVirtualKeyW(m.triggerVk, MAPVK_VK_TO_VSC);
        }
        for (auto &actToken : actions) {
            ActionSeq as = BuildActionForToken(actToken);
            if (!as.inputs.empty()) m.actions.push_back(as);
        }
        if (!m.actions.empty()) loaded.push_back(std::move(m));
    }
    return loaded;
}

static void LoadMappingsFromFile() {
    std::vector<Mapping> loaded;
    // use H:\progrem\mappings.txt
    std::wstring path = std::wstring(BASE_DIR) + L"\\mappings.txt";
    // ensure directory exists (best-effort)
    CreateDirectoryW(BASE_DIR, NULL);

    std::wifstream ifs(path.c_str());
    if (!ifs) {
        std::wstring msg = L"mappings.txt not found, no custom mappings loaded\n";
        OutputDebugStringW(msg.c_str());
    } else {
        ifs.imbue(std::locale::classic());
        std::wstring line;
        std::vector<std::wstring> lines;
        while (std::getline(ifs, line)) {
            line = trim(line);
            if (!line.empty()) lines.push_back(line);
        }
        loaded = ParseMappingsFromLines(lines);
    }

    {
        std::lock_guard<std::mutex> lk(g_mappings_mutex);
        g_mappings = std::move(loaded);
    }
    OutputDebugStringW(L"KeyMapper: mappings loaded/reloaded\n");
}

// Load mappings from an in-memory string (multiple lines) and apply
static void LoadMappingsFromString(const std::wstring& data) {
    std::vector<std::wstring> lines;
    std::wistringstream iss(data);
    std::wstring line;
    while (std::getline(iss, line)) {
        line = trim(line);
        if (!line.empty()) lines.push_back(line);
    }
    auto loaded = ParseMappingsFromLines(lines);
    {
        std::lock_guard<std::mutex> lk(g_mappings_mutex);
        g_mappings = std::move(loaded);
    }
    OutputDebugStringW(L"KeyMapper: mappings loaded from WM_COPYDATA\n");
}


// --- 函数声明 ---
void AddTrayIcon(HWND hWnd);
void RemoveTrayIcon(HWND hWnd);
void UpdateTrayMenu(HWND hWnd);
void SendMappedKeys(WORD vKey);
void ToggleActiveState(HWND hWnd);
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);


// --- 实用函数 ---

// 最终版本：精确控制 F6 和 F7 序列，并在末尾添加 Shift 切换输入法模式。
void SendMappedKeys(WORD vKey) {
    
    // 不再在映射后切换输入法（保持当前 IME 状态）
    
    // --- 1. 根据按键发送对应字符 ---
    
    if (vKey == VK_F6) {
        // F6: 模拟物理按键 'y' 的按下与释放（不按 Shift），
        // 以便微软拼音等输入法将其作为拼音字母接收并进入候选组合流程。
        // 使用扫描码注入（更接近物理按键），对虚拟/远程会话更稳健。
        WORD vkY = VkKeyScanW('y') & 0xff; // 只取低字节（虚拟键码）
        WORD scY = (WORD)MapVirtualKeyW(vkY, MAPVK_VK_TO_VSC);

        INPUT keyPress[2];
        ZeroMemory(keyPress, sizeof(keyPress));

        // 按下 'y'（通过扫描码）
        keyPress[0].type = INPUT_KEYBOARD;
        keyPress[0].ki.wScan = scY;
        keyPress[0].ki.dwExtraInfo = g_inject_marker;
        keyPress[0].ki.dwFlags = KEYEVENTF_SCANCODE;

        // 释放 'y'
        keyPress[1].type = INPUT_KEYBOARD;
        keyPress[1].ki.wScan = scY;
        keyPress[1].ki.dwExtraInfo = g_inject_marker;
        keyPress[1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

        SendInput(2, keyPress, sizeof(INPUT));
        Sleep(5);

    } else if (vKey == VK_F7) {
        // F7 -> '^' (必须 Shift + 6)
        
        // 使用扫描码进行 Shift + '6' 注入（^ 字符）。
        WORD vkShift = VK_SHIFT;
        WORD vk6 = VkKeyScanW('6') & 0xff;
        WORD scShift = (WORD)MapVirtualKeyW(vkShift, MAPVK_VK_TO_VSC);
        WORD sc6 = (WORD)MapVirtualKeyW(vk6, MAPVK_VK_TO_VSC);

        INPUT inputs[4];
        ZeroMemory(inputs, sizeof(inputs));

        // 按下 Shift 的扫描码
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wScan = scShift;
        inputs[0].ki.dwExtraInfo = g_inject_marker;
        inputs[0].ki.dwFlags = KEYEVENTF_SCANCODE;

        // 按下 '6' 的扫描码
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wScan = sc6;
        inputs[1].ki.dwExtraInfo = g_inject_marker;
        inputs[1].ki.dwFlags = KEYEVENTF_SCANCODE;

        // 释放 '6'
        inputs[2].type = INPUT_KEYBOARD;
        inputs[2].ki.wScan = sc6;
        inputs[2].ki.dwExtraInfo = g_inject_marker;
        inputs[2].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

        // 释放 Shift
        inputs[3].type = INPUT_KEYBOARD;
        inputs[3].ki.wScan = scShift;
        inputs[3].ki.dwExtraInfo = g_inject_marker;
        inputs[3].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

        SendInput(4, inputs, sizeof(INPUT));
        Sleep(5);
    }
}
// 键盘钩子回调函数 (按键重复控制)
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pKey = (KBDLLHOOKSTRUCT*)lParam;
        DWORD vkCode = pKey->vkCode;
        DWORD scanCode = pKey->scanCode;

        // 在某些虚拟机或远程会话中，vkCode 可能不可靠或为 0，
        // 而 scanCode（硬件扫描码）会被保留。这里同时检查 vkCode 与 scanCode。
        bool is_f6_event = (vkCode == VK_F6) || (scanCode == 0x40); // F6 scan code = 0x40
        bool is_f7_event = (vkCode == VK_F7) || (scanCode == 0x41); // F7 scan code = 0x41

        // 忽略由本程序通过 SendInput 注入的事件，以避免死循环；
        // 但保留对其它注入事件（例如远程控制软件注入）的处理，以便能拦截并映射这些按键。
        if (pKey->flags & (LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED)) {
            // 只有当注入事件的 dwExtraInfo 与我们自己的标记相同时才忽略（说明是本程序注入）
            if (pKey->dwExtraInfo == g_inject_marker) {
                OutputDebugStringW(L"KeyHook: ignored self-injected event\n");
                return CallNextHookEx(hHook, nCode, wParam, lParam);
            }
            // 如果不是我们的注入（dwExtraInfo 不同），则允许继续处理，以便捕获来自远程客户端或第三方的注入事件
            OutputDebugStringW(L"KeyHook: observed external-injected event (will be processed)\n");
        }

        // 先检查用户自定义映射表，优先处理自定义映射
        {
            std::lock_guard<std::mutex> lk(g_mappings_mutex);
            for (auto &m : g_mappings) {
                if (m.triggerVk == vkCode || m.triggerScan == scanCode) {
                    if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
                        if (is_active) {
                            bool should_send = repeat_on_hold || !m.is_pressed;
                            if (should_send) {
                                m.is_pressed = true;
                                for (auto &act : m.actions) {
                                    if (!act.inputs.empty()) {
                                        SendInput((UINT)act.inputs.size(), (LPINPUT)act.inputs.data(), sizeof(INPUT));
                                        Sleep(2);
                                    }
                                }
                            }
                        }
                        return 1; // 屏蔽原始按键
                    } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
                        m.is_pressed = false;
                        return 1;
                    }
                }
            }
        }

        if (is_f6_event || is_f7_event) {
            // 调试输出：方便在远程/VM 中观察 vkCode/scanCode/flags
            // 你可以用 DebugView 或者用自己的工具查看这些输出
            wchar_t dbg[512];
            // 输出更多信息：包含 dwExtraInfo，这能帮助我们区分注入来源（本程序/远程/其它）
            _snwprintf(dbg, _countof(dbg), L"KeyHook: vk=%u scan=%u flags=0x%X dwExtraInfo=0x%p wParam=%u\n", vkCode, scanCode, pKey->flags, (PVOID)pKey->dwExtraInfo, (UINT)wParam);
            OutputDebugStringW(dbg);
            // 选择对应的状态标记，如果是扫描码匹配，则按键类型也由该扫描码决定
            bool* is_pressed_flag = is_f6_event ? &is_f6_pressed : &is_f7_pressed;
            WORD effectiveVk = is_f6_event ? VK_F6 : VK_F7;

            // 注意：远程/VM 注入事件可能带有 LLKHF_INJECTED 标志 (0x10) 或 LLKHF_LOWER_IL_INJECTED (0x02)
            // 如果你希望忽略注入事件，可以检测 pKey->flags & LLKHF_INJECTED。这里保留默认行为。

            if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
                // 在按下时，如果启用了 repeat_on_hold 则每次 KEYDOWN 都发送；
                // 否则仅在第一次按下时发送（保持触发式行为以避免自动重复）。
                if (is_active) {
                    bool should_send = repeat_on_hold || !*is_pressed_flag;
                    if (should_send) {
                        *is_pressed_flag = true; // 标记为已按下
                        SendMappedKeys(effectiveVk);
                    }
                }
                // 屏蔽所有 F6/F7 按下事件 (包括首次和自动重复)
                return 1;
            } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
                // 释放事件发生时，重置状态
                *is_pressed_flag = false;
                // 屏蔽所有 F6/F7 释放事件
                return 1;
            }
        }
    }
    // 对于其他按键或未处理的事件，传递给链中的下一个钩子
    return CallNextHookEx(hHook, nCode, wParam, lParam);
}

// 启用/禁用功能
void ToggleActiveState(HWND hWnd) {
    is_active = !is_active;
    UpdateTrayMenu(hWnd);
}

// --- better f6 to y：注册表级按键重映射（按下 Y 键 -> 系统收到 F6） ---
// 对应 f6.reg 写入的 Scancode Map（HKLM\SYSTEM\CurrentControlSet\Control\Keyboard Layout）
// 20 字节：版本/标志/条目数 2（1 条映射 + 终止符），映射 DWORD 0x00400015（新键 0x40=F6，旧键 0x15=Y）
static const BYTE kF6YScancodeMap[20] = {
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x02,0x00,0x00,0x00,
    0x15,0x00,0x40,0x00, 0x00,0x00,0x00,0x00
};

// 检测注册表中当前是否写入了本功能的 Scancode Map（逐字节比对）
static bool IsF6YMappingActive() {
    HKEY hk = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layout",
                      0, KEY_QUERY_VALUE, &hk) != ERROR_SUCCESS) return false;
    BYTE buf[32] = {};
    DWORD type = 0, cb = sizeof(buf);
    bool active = false;
    if (RegQueryValueExW(hk, L"Scancode Map", NULL, &type, buf, &cb) == ERROR_SUCCESS &&
        type == REG_BINARY && cb == sizeof(kF6YScancodeMap) &&
        memcmp(buf, kF6YScancodeMap, sizeof(kF6YScancodeMap)) == 0) {
        active = true;
    }
    RegCloseKey(hk);
    return active;
}

// 启用 = 写入 Scancode Map（等价导入 f6.reg）；关闭 = 删除该值（等价导入 恢复.reg）
static bool ApplyF6YMapping(bool enable) {
    HKEY hk = NULL;
    LONG r = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layout",
                           0, KEY_SET_VALUE, &hk);
    if (r != ERROR_SUCCESS) return false;
    if (enable) {
        r = RegSetValueExW(hk, L"Scancode Map", 0, REG_BINARY, kF6YScancodeMap, sizeof(kF6YScancodeMap));
    } else {
        r = RegDeleteValueW(hk, L"Scancode Map");
        if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS; // 本来就是关闭状态，视为成功
    }
    RegCloseKey(hk);
    return r == ERROR_SUCCESS;
}

// 通过托盘图标弹出气泡通知（Win10+ 会以系统通知形式显示）
static void ShowTrayBalloon(const wchar_t *title, const wchar_t *text) {
    nid.uFlags |= NIF_INFO;
    wcsncpy(nid.szInfo, text, 255);      nid.szInfo[255] = L'\0';
    wcsncpy(nid.szInfoTitle, title, 63); nid.szInfoTitle[63] = L'\0';
    nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

// --- 托盘图标和菜单函数 ---

// 修正：使用传入的 hWnd 作为图标宿主
void AddTrayIcon(HWND hWnd) { 
    
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    
    // 使用稳定的主窗口句柄
    nid.hWnd = hWnd; 
    
    nid.uID = ID_TRAY_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; 
    nid.uCallbackMessage = WM_TRAY_ICON_MESSAGE;   
    
    // 设置图标
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    
    // 修正：使用 wcsncpy 替换有兼容性问题的 wcscpy_s
    wcsncpy(nid.szTip, L"键盘映射器 - F6/F7", 128);
    nid.szTip[127] = L'\0'; // 确保 NUL 终止

    // 添加图标到任务栏
    Shell_NotifyIcon(NIM_ADD, &nid);
}

void RemoveTrayIcon(HWND /*hWnd*/) {
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

void UpdateTrayMenu(HWND hWnd) {
    HMENU hMenu = CreatePopupMenu();
    
    // 创建菜单项：启用/禁用映射
    UINT stateFlags = MF_STRING;
    if (is_active) {
        stateFlags |= MF_CHECKED; 
    }
    InsertMenuW(hMenu, 0, MF_BYPOSITION | stateFlags, ID_ENABLE_DISABLE, L"启用/禁用 映射");

    // 添加按住重复触发选项
    UINT repeatFlags = MF_BYPOSITION | MF_STRING;
    if (repeat_on_hold) repeatFlags |= MF_CHECKED;
    InsertMenuW(hMenu, 1, repeatFlags, ID_TOGGLE_REPEAT, L"按住时重复触发 (F6/F7)");

    // better f6 to y：注册表级 Y->F6 重映射，勾选状态实时读注册表
    UINT f6yFlags = MF_BYPOSITION | MF_STRING;
    if (IsF6YMappingActive()) f6yFlags |= MF_CHECKED;
    InsertMenuW(hMenu, 2, f6yFlags, ID_TOGGLE_F6Y, L"better f6 to y");

    // 分隔符
    InsertMenuW(hMenu, 3, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);

    // 修正：明确调用 InsertMenuW (Unicode)
    InsertMenuW(hMenu, 4, MF_BYPOSITION | MF_STRING, ID_QUIT, L"退出");
    
    POINT pt;
    GetCursorPos(&pt);
    
    SetForegroundWindow(hWnd); 
    
    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hWnd, NULL);
    
    DestroyMenu(hMenu);
}

// --- 窗口过程函数 ---
LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            // 设置全局低级键盘钩子
            hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);

            if (hHook == NULL) {
                // 修正：明确调用 MessageBoxW (Unicode)
                MessageBoxW(NULL, L"安装键盘钩子失败！请以管理员身份运行程序。", L"错误", MB_ICONERROR);
                PostQuitMessage(0); 
            }
            // 传递稳定的窗口句柄
            AddTrayIcon(hWnd);
            // 初次加载映射表（如果存在 mappings.txt）
            LoadMappingsFromFile();
            break;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case ID_ENABLE_DISABLE:
                    ToggleActiveState(hWnd);
                    break;
                case ID_TOGGLE_REPEAT:
                    repeat_on_hold = !repeat_on_hold;
                    UpdateTrayMenu(hWnd);
                    break;
                case ID_TOGGLE_F6Y: {
                    bool enable = !IsF6YMappingActive();
                    if (ApplyF6YMapping(enable)) {
                        // 启用和关闭都需要重启计算机才会生效
                        ShowTrayBalloon(L"better f6 to y",
                            enable ? L"已启用（Y 键 -> F6）：需要重启计算机后生效。"
                                   : L"已关闭（恢复默认按键）：需要重启计算机后生效。");
                    } else {
                        MessageBoxW(NULL, L"写入注册表失败：请以管理员身份运行本程序后重试。", L"better f6 to y", MB_ICONERROR);
                    }
                    break;
                }
                case ID_QUIT:
                    PostQuitMessage(0);
                    break;
            }
            break;
        }

        case WM_TRAY_ICON_MESSAGE: {
            switch (LOWORD(lParam)) {
                case WM_RBUTTONUP: 
                    UpdateTrayMenu(hWnd);
                    break;
            }
            break;
        }

        case WM_RELOAD_MAPPINGS: {
            LoadMappingsFromFile();
            break;
        }

        case WM_DESTROY: {
            if (hHook) UnhookWindowsHookEx(hHook);
            RemoveTrayIcon(hWnd);
            PostQuitMessage(0);
            break;
        }

        case WM_COPYDATA: {
            PCOPYDATASTRUCT pcd = (PCOPYDATASTRUCT)lParam;
            if (!pcd) break;
            // dwData == 0xBEEF -> payload is full mappings content (wide string)
            if (pcd->dwData == 0xBEEF) {
                const wchar_t *data = (const wchar_t*)pcd->lpData;
                if (data) {
                    // save to disk for persistence
                    std::wstring path = std::wstring(BASE_DIR) + L"\\mappings.txt";
                    std::wofstream ofs(path.c_str());
                    ofs.imbue(std::locale::classic());
                    ofs << data;
                    ofs.close();
                    LoadMappingsFromString(data);
                }
            } else if (pcd->dwData == 0xB003) {
                // TOGGLE:<triggerName>
                const wchar_t *data = (const wchar_t*)pcd->lpData;
                if (data) {
                    std::wstring s(data);
                    const std::wstring cmd = L"TOGGLE:";
                    if (s.rfind(cmd, 0) == 0) {
                        std::wstring trg = s.substr(cmd.size());
                        std::lock_guard<std::mutex> lk(g_mappings_mutex);
                        for (auto &m : g_mappings) {
                            if (m.triggerName == trg) {
                                m.enabled = !m.enabled;
                                std::wstring dbg; dbg.resize(256);
                                _snwprintf(&dbg[0], 256, L"Toggled mapping %s -> %d\n", trg.c_str(), (int)m.enabled);
                                OutputDebugStringW(dbg.c_str());
                                break;
                            }
                        }
                    }
                }
            }
            break;
        }

        default:
            return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
    return 0;
}

// --- WinMain 主函数 (Win32 Entry Point) ---
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR /*lpCmdLine*/, int /*nCmdShow*/) {
    const wchar_t CLASS_NAME[] = L"HiddenKeyMapperClass";
    
    // 在程序启动时生成一个独一无二的标记值，用作我们自己注入事件的 dwExtraInfo
    // 这样钩子可以只忽略带有该标记的注入，允许处理其它（如远程客户端）注入事件。
    g_inject_marker = (ULONG_PTR)GetCurrentProcessId();
    g_inject_marker ^= (ULONG_PTR)GetTickCount64();

    // 1. 注册窗口类
    WNDCLASSW wc = {}; // WNDCLASSW 是 WNDCLASS 在 Unicode 模式下的显式名称
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME; 

    // 修正：明确调用 RegisterClassW (Unicode)
    RegisterClassW(&wc); 

    // 2. 创建隐藏的主窗口
    // 修正：明确调用 CreateWindowExW (Unicode)
    HWND hWnd = CreateWindowExW(
        0,                               
        CLASS_NAME,                      
        L"C++ Key Mapper",               
        0,                               // 样式 0 = 完全隐藏
        CW_USEDEFAULT, CW_USEDEFAULT,     
        CW_USEDEFAULT, CW_USEDEFAULT,     
        NULL,                            
        NULL,                            
        hInstance,                       
        NULL                             
    );

    if (hWnd == NULL) {
        return 0;
    }
    
    // 3. 消息循环
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}


// --- 解决链接错误：MinGW x64 入口点修正 ---
int main() {
    // 使用 GetCommandLineA() 来匹配 WinMain 的 LPSTR 参数。
    return WinMain(GetModuleHandle(NULL), NULL, GetCommandLineA(), SW_SHOWNORMAL);
}