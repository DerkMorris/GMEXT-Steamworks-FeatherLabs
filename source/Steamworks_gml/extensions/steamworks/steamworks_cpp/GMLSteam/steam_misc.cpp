/// steam_misc.cpp
#include "pch.h"
#include "steam_glue.h"
#include "steam_api.h"
#include "Extension_Interface.h"
#include "YYRValue.h"
#include "steam_common.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#ifdef OS_Windows
#include <winsock2.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <unistd.h>
#endif

// #pragma region int64 workarounds (http://bugs.yoyogames.com/view.php?id=21357)
// An extremely non-picky parser. Will combine up to 20 digits from
// an input string into an int64, skipping any other characters.
/*
uint64 int64_from_string(char* cstring) {
	char c;
	int start = -1, end = -1;
	for (int pos = 0; (c = cstring[pos]) != '\0'; pos++) {
		if (c >= '0' && c <= '9') {
			if (start < 0) start = pos;
			end = pos;
		}
	}
	uint64 out = 0;
	if (start < 0) return out;
	uint64 mul = 1;
	int digit = 0;
	for (int pos = end; pos >= start; pos--) {
		c = cstring[pos];
		if (c >= '0' && c <= '9') {
			out += ((uint64)(c - '0')) * mul;
			mul *= 10;
			if (++digit >= 20) return out;
		}
	}
	return out;
}
*/

void steam_lobby_chat_update();
YYEXPORT void /*double*/ steam_gml_update(RValue& Result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)//()
{
	SteamAPI_RunCallbacks();
	steam_lobby_chat_update();
}

/// Detects if the app was run from Steam client and restarts if needed. Returns whether app should quit.
YYEXPORT void /*double*/ steam_restart_if_necessary(RValue& Result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)//() 
{
	Result.kind = VALUE_REAL;
	Result.val = SteamAPI_RestartAppIfNecessary(steam_app_id);
}

bool steam_gml_ready = false;
YYEXPORT void /*double*/ steam_gml_api_flags(RValue& Result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)//() 
{
	int r = 0;
	if (steam_gml_ready) r |= 1;
	if (SteamUtils()) r |= 2;
	if (SteamUser()) r |= 4;
	if (SteamFriends()) r |= 8;
	if (SteamNetworking()) r |= 16;
	if (SteamMatchmaking()) r |= 32;
	if (SteamController()) r |= 64;
	if (SteamUGC()) r |= 128;


	Result.kind = VALUE_REAL;
	Result.val = r;
}

//YYEXPORT void /*double*/ steam_gml_init_cpp(RValue& Result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)//(double app_id) 
//{
//	double app_id = YYGetReal(arg, 0);
//
//	steam_app_id = (uint32) app_id;
//	if (!SteamAPI.Init()) {
//		DebugConsoleOutput("Steamworks.gml failed to link with Steam API.");
//		{
//			Result.kind = VALUE_REAL;
//			Result.val = 0;
//			return;
//		}
//	}
//	steam_gml_ready = true;
//	steam_local_id = SteamUser()->GetSteamID();
//	DebugConsoleOutput("Steamworks.gml initialized successfully.");
//
//	Result.kind = VALUE_REAL;
//	Result.val = 1;
//	return;
//}

/// Returns whether the extension has initialized successfully.
//YYEXPORT void /*double*/ steam_gml_is_ready(RValue& Result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)//() 
//{
//	Result.kind = VALUE_REAL;
//	Result.val = steam_gml_ready;
//}
//
//YYEXPORT void /*double*/ steam_gml_get_version(RValue& Result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)//() 
//{
//	Result.kind = VALUE_REAL;
//	Result.val = steam_net_version;
//}
//
///// Returns whether the extension was loaded at all (GML returns 0 for unloaded extension calls).
//YYEXPORT void /*double*/ steam_gml_is_available(RValue& Result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)//() 
//{
//	Result.kind = VALUE_REAL;
//	Result.val = 1;
//}
//
//void steam_controller_reset_impl();
//YYEXPORT void /*double*/ steam_gml_init_cpp_pre(RValue& Result, CInstance* selfinst, CInstance* otherinst, int argc, RValue* arg)//() 
//{
//	DebugConsoleOutput("Steamworks.gml loaded native extension.");
//	steam_controller_reset_impl();
//	steam_lobby_current.Clear();
//	
//	Result.kind = VALUE_REAL;
//	Result.val = 1;
//}

namespace
{
void add_local_ipv4_entry(std::vector<RValue>& entries, const char* address,
    const char* adapterName, int prefixLength, bool isUp, bool isLoopback)
{
    RValue item{};
    YYStructCreate(&item);
    YYStructAddString(&item, "address", address ? address : "");
    YYStructAddString(&item, "adapter_name", adapterName ? adapterName : "");
    YYStructAddInt(&item, "prefix_length", prefixLength);
    YYStructAddBool(&item, "is_up", isUp);
    YYStructAddBool(&item, "is_loopback", isLoopback);
    entries.push_back(item);
}

#ifdef OS_Windows
std::string wide_to_utf8(const wchar_t* value)
{
    if (!value || !*value)
        return std::string();

    int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
        return std::string();

    std::vector<char> bytes(static_cast<size_t>(size));
    if (WideCharToMultiByte(CP_UTF8, 0, value, -1, bytes.data(), size, nullptr, nullptr) == 0)
        return std::string();
    return std::string(bytes.data());
}
#endif
}

/// @description Feather Labs Misc: returns local IPv4 addresses for all network adapters.
/// @returns {Array<Struct>} Array of { address, adapter_name, prefix_length, is_up, is_loopback }.
YYEXPORT void network_get_local_ipv4_addresses(RValue& Result, CInstance*, CInstance*, int, RValue*)
{
    YYCreateArray(&Result);
    std::vector<RValue> entries;

#ifdef OS_Windows
    ULONG bufferSize = 15 * 1024;
    std::vector<unsigned char> buffer(bufferSize);
    PIP_ADAPTER_ADDRESSES adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
    ULONG status = GetAdaptersAddresses(AF_INET,
        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
        nullptr, adapters, &bufferSize);

    if (status == ERROR_BUFFER_OVERFLOW)
    {
        buffer.resize(bufferSize);
        adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
        status = GetAdaptersAddresses(AF_INET,
            GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            nullptr, adapters, &bufferSize);
    }

    if (status == NO_ERROR)
    {
        for (PIP_ADAPTER_ADDRESSES adapter = adapters; adapter; adapter = adapter->Next)
        {
            std::string adapterName = wide_to_utf8(adapter->FriendlyName);
            if (adapterName.empty() && adapter->AdapterName)
                adapterName = adapter->AdapterName;

            const bool isUp = adapter->OperStatus == IfOperStatusUp;
            const bool adapterLoopback = adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK;

            for (PIP_ADAPTER_UNICAST_ADDRESS unicast = adapter->FirstUnicastAddress;
                unicast; unicast = unicast->Next)
            {
                if (!unicast->Address.lpSockaddr || unicast->Address.lpSockaddr->sa_family != AF_INET)
                    continue;

                const sockaddr_in* socketAddress =
                    reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
                const unsigned char* octets =
                    reinterpret_cast<const unsigned char*>(&socketAddress->sin_addr.S_un.S_addr);
                char address[16]{};
                std::snprintf(address, sizeof(address), "%u.%u.%u.%u",
                    static_cast<unsigned>(octets[0]), static_cast<unsigned>(octets[1]),
                    static_cast<unsigned>(octets[2]), static_cast<unsigned>(octets[3]));
                const bool isLoopback = adapterLoopback || octets[0] == 127;
                add_local_ipv4_entry(entries, address, adapterName.c_str(),
                    static_cast<int>(unicast->OnLinkPrefixLength), isUp, isLoopback);
            }
        }
    }
#else
    ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces) == 0)
    {
        for (ifaddrs* interfaceAddress = interfaces; interfaceAddress;
            interfaceAddress = interfaceAddress->ifa_next)
        {
            if (!interfaceAddress->ifa_addr || interfaceAddress->ifa_addr->sa_family != AF_INET)
                continue;

            const sockaddr_in* socketAddress =
                reinterpret_cast<const sockaddr_in*>(interfaceAddress->ifa_addr);
            char address[INET_ADDRSTRLEN]{};
            if (!inet_ntop(AF_INET, &socketAddress->sin_addr, address, sizeof(address)))
                continue;

            int prefixLength = 0;
            if (interfaceAddress->ifa_netmask)
            {
                const sockaddr_in* netmask =
                    reinterpret_cast<const sockaddr_in*>(interfaceAddress->ifa_netmask);
                uint32_t mask = ntohl(netmask->sin_addr.s_addr);
                while ((mask & 0x80000000u) != 0)
                {
                    ++prefixLength;
                    mask <<= 1;
                }
            }

            add_local_ipv4_entry(entries, address, interfaceAddress->ifa_name,
                prefixLength,
                (interfaceAddress->ifa_flags & IFF_UP) != 0,
                (interfaceAddress->ifa_flags & IFF_LOOPBACK) != 0);
        }
        freeifaddrs(interfaces);
    }
#endif

    _SW_SetArrayOfRValue(&Result, entries);
}

/// @description Converts an IPv4 address in dotted notation to its numeric host-order value.
/// @param {String} ip IPv4 address such as "192.168.1.20".
/// @returns {Int64} Value suitable for steam_game_server_init, or -1 if the address is invalid.
YYEXPORT void network_ipv4_to_number(RValue& Result, CInstance*, CInstance*, int argc, RValue* args)
{
    Result.kind = VALUE_INT64;
    Result.v64 = -1;

    if (argc < 1)
        return;

    const char* input = YYGetString(args, 0);
    if (!input || !*input)
        return;

    uint32_t address = 0;
    const char* cursor = input;

    for (int octetIndex = 0; octetIndex < 4; ++octetIndex)
    {
        if (*cursor < '0' || *cursor > '9')
            return;

        uint32_t octet = 0;
        do
        {
            octet = octet * 10u + static_cast<uint32_t>(*cursor - '0');
            if (octet > 255u)
                return;
            ++cursor;
        }
        while (*cursor >= '0' && *cursor <= '9');

        address = (address << 8u) | octet;

        if (octetIndex < 3)
        {
            if (*cursor != '.')
                return;
            ++cursor;
        }
        else if (*cursor != '\0')
        {
            return;
        }
    }

    Result.v64 = static_cast<int64_t>(address);
}

/// @description Converts a numeric host-order IPv4 value to dotted notation.
/// @param {Int64} ip Numeric IPv4 value from 0 through 4294967295.
/// @returns {String} IPv4 address, or an empty string if the value is invalid.
YYEXPORT void network_number_to_ipv4(RValue& Result, CInstance*, CInstance*, int argc, RValue* args)
{
    if (argc < 1)
    {
        YYCreateString(&Result, "");
        return;
    }

    const int64 value = YYGetInt64(args, 0);
    if (value < 0 || value > 0xFFFFFFFFLL)
    {
        YYCreateString(&Result, "");
        return;
    }

    const uint32_t address = static_cast<uint32_t>(value);
    char text[16]{};
    std::snprintf(text, sizeof(text), "%u.%u.%u.%u",
        static_cast<unsigned>((address >> 24u) & 255u),
        static_cast<unsigned>((address >> 16u) & 255u),
        static_cast<unsigned>((address >> 8u) & 255u),
        static_cast<unsigned>(address & 255u));
    YYCreateString(&Result, text);
}

namespace
{
#ifdef OS_Windows
HWND window_handle_from_rvalue(RValue* value)
{
    if (!value)
        return nullptr;

    uintptr_t handle = 0;
    switch (KIND_RValue(value))
    {
        case VALUE_PTR:
            handle = reinterpret_cast<uintptr_t>(value->ptr);
            break;
        case VALUE_INT64:
            handle = static_cast<uintptr_t>(value->v64);
            break;
        case VALUE_INT32:
            handle = static_cast<uintptr_t>(
                static_cast<uint32_t>(value->v32));
            break;
        case VALUE_REAL:
            handle = static_cast<uintptr_t>(value->val);
            break;
        case VALUE_STRING:
        {
            const char* text = value->GetString();
            char* end = nullptr;
            handle = static_cast<uintptr_t>(
                std::strtoull(text ? text : "", &end, 0));
            if (!text || end == text || *end != '\0')
                return nullptr;
            break;
        }
        default:
            return nullptr;
    }

    HWND window = reinterpret_cast<HWND>(handle);
    return window && IsWindow(window) ? window : nullptr;
}
#endif

bool consoleActive = false;
std::deque<std::string> consoleLines;
int consoleTextColor = -1;
int consoleBackgroundColor = -1;
std::string consolePrompt = "> ";
bool consolePromptVisible = false;

#ifdef OS_Windows
bool consoleOwned = false;
HANDLE consoleInput = INVALID_HANDLE_VALUE;
HANDLE consoleOutput = INVALID_HANDLE_VALUE;
std::wstring consoleInputLine;
DWORD originalConsoleOutputMode = 0;
bool consoleOutputModeSaved = false;
bool consoleVirtualTerminal = false;
WORD defaultConsoleAttributes =
    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

std::wstring utf8_to_wide(const char* value)
{
    if (!value || !*value)
        return std::wstring();

    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value, -1, nullptr, 0);
    if (size <= 1)
        return std::wstring();

    std::vector<wchar_t> characters(static_cast<size_t>(size));
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value, -1, characters.data(), size) == 0)
        return std::wstring();
    return std::wstring(characters.data());
}

bool write_console_wide(const wchar_t* value, DWORD length)
{
    if (!consoleActive || consoleOutput == INVALID_HANDLE_VALUE)
        return false;

    DWORD written = 0;
    return WriteConsoleW(consoleOutput, value, length, &written, nullptr) != FALSE &&
        written == length;
}

bool show_console_prompt()
{
    if (consolePromptVisible)
        return true;

    const std::wstring prompt = utf8_to_wide(consolePrompt.c_str());
    if (!consolePrompt.empty() && prompt.empty())
        return false;

    bool success = prompt.empty() ||
        write_console_wide(prompt.c_str(), static_cast<DWORD>(prompt.size()));
    if (success && !consoleInputLine.empty())
        success = write_console_wide(consoleInputLine.c_str(),
            static_cast<DWORD>(consoleInputLine.size()));
    consolePromptVisible = success;
    return success;
}

void clear_console_prompt_visual()
{
    if (!consolePromptVisible)
        return;

    const std::wstring prompt = utf8_to_wide(consolePrompt.c_str());
    const size_t visibleLength = prompt.size() + consoleInputLine.size();
    static const wchar_t carriageReturn = L'\r';
    write_console_wide(&carriageReturn, 1);
    if (visibleLength > 0)
    {
        const std::wstring spaces(visibleLength, L' ');
        write_console_wide(spaces.c_str(), static_cast<DWORD>(spaces.size()));
        write_console_wide(&carriageReturn, 1);
    }
    consolePromptVisible = false;
}

WORD windows_color_bits(int color, bool background)
{
    static const WORD foregroundColors[8] = {
        0,
        FOREGROUND_RED,
        FOREGROUND_GREEN,
        FOREGROUND_RED | FOREGROUND_GREEN,
        FOREGROUND_BLUE,
        FOREGROUND_RED | FOREGROUND_BLUE,
        FOREGROUND_GREEN | FOREGROUND_BLUE,
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE
    };

    WORD bits = foregroundColors[color & 7];
    if ((color & 8) != 0)
        bits |= FOREGROUND_INTENSITY;
    return background ? static_cast<WORD>(bits << 4) : bits;
}

bool apply_console_colors()
{
    if (!consoleActive || consoleOutput == INVALID_HANDLE_VALUE)
        return false;

    if (consoleVirtualTerminal)
    {
        const int foreground = consoleTextColor < 0 ? 39 :
            (consoleTextColor < 8 ? 30 + consoleTextColor : 90 + consoleTextColor - 8);
        const int background = consoleBackgroundColor < 0 ? 49 :
            (consoleBackgroundColor < 8 ? 40 + consoleBackgroundColor : 100 + consoleBackgroundColor - 8);
        wchar_t sequence[24]{};
        const int length = std::swprintf(sequence,
            sizeof(sequence) / sizeof(sequence[0]), L"\x1b[%d;%dm",
            foreground, background);
        return length > 0 &&
            write_console_wide(sequence, static_cast<DWORD>(length));
    }

    const WORD foregroundMask = FOREGROUND_RED | FOREGROUND_GREEN |
        FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    const WORD backgroundMask = BACKGROUND_RED | BACKGROUND_GREEN |
        BACKGROUND_BLUE | BACKGROUND_INTENSITY;
    WORD attributes = defaultConsoleAttributes;
    if (consoleTextColor >= 0)
        attributes = static_cast<WORD>((attributes & ~foregroundMask) |
            windows_color_bits(consoleTextColor, false));
    if (consoleBackgroundColor >= 0)
        attributes = static_cast<WORD>((attributes & ~backgroundMask) |
            windows_color_bits(consoleBackgroundColor, true));
    return SetConsoleTextAttribute(consoleOutput, attributes) != FALSE;
}

bool write_console_line(const char* text)
{
    if (!consoleActive)
        return false;

    clear_console_prompt_visual();
    const std::wstring line = utf8_to_wide(text ? text : "");
    if (text && *text && line.empty())
        return false;

    bool success = line.empty() ||
        write_console_wide(line.c_str(), static_cast<DWORD>(line.size()));
    static const wchar_t newline[] = L"\r\n";
    return write_console_wide(newline, 2) && success;
}

void poll_console_input()
{
    if (!consoleActive || consoleInput == INVALID_HANDLE_VALUE)
        return;
    if (!show_console_prompt())
        return;

    DWORD available = 0;
    if (!GetNumberOfConsoleInputEvents(consoleInput, &available))
        return;

    while (available > 0)
    {
        INPUT_RECORD records[64]{};
        DWORD read = 0;
        const DWORD requested = available < 64 ? available : 64;
        if (!ReadConsoleInputW(consoleInput, records, requested, &read))
            return;

        for (DWORD recordIndex = 0; recordIndex < read; ++recordIndex)
        {
            const INPUT_RECORD& record = records[recordIndex];
            if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown)
                continue;

            const KEY_EVENT_RECORD& key = record.Event.KeyEvent;
            const wchar_t character = key.uChar.UnicodeChar;
            const WORD repeats = key.wRepeatCount > 0 ? key.wRepeatCount : 1;

            for (WORD repeat = 0; repeat < repeats; ++repeat)
            {
                if (character == L'\r' || character == L'\n')
                {
                    static const wchar_t newline[] = L"\r\n";
                    write_console_wide(newline, 2);
                    consolePromptVisible = false;
                    if (!consoleInputLine.empty())
                    {
                        consoleLines.push_back(wide_to_utf8(consoleInputLine.c_str()));
                        consoleInputLine.clear();
                    }
                }
                else if (character == L'\b')
                {
                    if (!consoleInputLine.empty())
                    {
                        consoleInputLine.pop_back();
                        if (!consoleInputLine.empty() &&
                            consoleInputLine.back() >= 0xD800 &&
                            consoleInputLine.back() <= 0xDBFF)
                            consoleInputLine.pop_back();
                        static const wchar_t erase[] = L"\b \b";
                        write_console_wide(erase, 3);
                    }
                }
                else if (character >= L' ')
                {
                    consoleInputLine.push_back(character);
                    write_console_wide(&character, 1);
                }
            }
        }

        if (!GetNumberOfConsoleInputEvents(consoleInput, &available))
            return;
    }
}
#else
std::string consoleInputBytes;

bool show_console_prompt()
{
    if (consolePromptVisible)
        return true;
    const bool success =
        std::fwrite(consolePrompt.data(), 1, consolePrompt.size(), stdout) ==
        consolePrompt.size();
    std::fflush(stdout);
    consolePromptVisible = success;
    return success;
}

bool apply_console_colors()
{
    if (!consoleActive)
        return false;

    const int foreground = consoleTextColor < 0 ? 39 :
        (consoleTextColor < 8 ? 30 + consoleTextColor : 90 + consoleTextColor - 8);
    const int background = consoleBackgroundColor < 0 ? 49 :
        (consoleBackgroundColor < 8 ? 40 + consoleBackgroundColor : 100 + consoleBackgroundColor - 8);
    const bool success = std::fprintf(stdout, "\033[%d;%dm",
        foreground, background) >= 0;
    std::fflush(stdout);
    return success;
}

bool write_console_line(const char* text)
{
    if (!consoleActive)
        return false;

    if (consolePromptVisible)
    {
        std::fwrite("\n", 1, 1, stdout);
        consolePromptVisible = false;
    }
    const char* line = text ? text : "";
    const size_t length = std::strlen(line);
    const bool success = std::fwrite(line, 1, length, stdout) == length &&
        std::fwrite("\n", 1, 1, stdout) == 1;
    std::fflush(stdout);
    return success;
}

void poll_console_input()
{
    if (!consoleActive)
        return;
    if (!show_console_prompt())
        return;

    char buffer[512];
    for (;;)
    {
        pollfd inputStatus{};
        inputStatus.fd = STDIN_FILENO;
        inputStatus.events = POLLIN;
        if (poll(&inputStatus, 1, 0) <= 0 || (inputStatus.revents & POLLIN) == 0)
            break;

        const ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
        if (count > 0)
        {
            consoleInputBytes.append(buffer, static_cast<size_t>(count));
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        break;
    }

    size_t newline = 0;
    while ((newline = consoleInputBytes.find('\n')) != std::string::npos)
    {
        std::string line = consoleInputBytes.substr(0, newline);
        consoleInputBytes.erase(0, newline + 1);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty())
            consoleLines.push_back(line);
        consolePromptVisible = false;
    }
}
#endif

bool set_console_text_color_impl(int color)
{
    if (!consoleActive || color < 0 || color > 15)
        return false;
    const int previous = consoleTextColor;
    consoleTextColor = color;
    if (apply_console_colors())
        return true;
    consoleTextColor = previous;
    return false;
}

bool set_console_background_color_impl(int color)
{
    if (!consoleActive || color < 0 || color > 15)
        return false;
    const int previous = consoleBackgroundColor;
    consoleBackgroundColor = color;
    if (apply_console_colors())
        return true;
    consoleBackgroundColor = previous;
    return false;
}

void set_console_bool(RValue& result, bool value)
{
    result.kind = VALUE_BOOL;
    result.val = value;
}
}

/// @description Shows or hides a native GameMaker window on Windows.
/// @param {Pointer|String} handle Value returned by window_handle(); use string(window_handle()) for DLL safety.
/// @param {Bool} visible Whether the window should be visible.
/// @returns {Bool} Whether the requested visibility was applied.
YYEXPORT void window_set_visible(RValue& Result, CInstance*, CInstance*, int argc, RValue* args)
{
#ifdef OS_Windows
    if (argc < 2)
    {
        set_console_bool(Result, false);
        return;
    }

    HWND window = window_handle_from_rvalue(&args[0]);
    if (!window)
    {
        set_console_bool(Result, false);
        return;
    }

    const bool visible = YYGetBool(args, 1);
    ShowWindow(window, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    set_console_bool(Result,
        (IsWindowVisible(window) != FALSE) == visible);
#else
    set_console_bool(Result, false);
#endif
}

/// @description Returns whether a native GameMaker window is visible on Windows.
/// @param {Pointer|String} handle Value returned by window_handle(); use string(window_handle()) for DLL safety.
/// @returns {Bool}
YYEXPORT void window_get_visible(RValue& Result, CInstance*, CInstance*, int argc, RValue* args)
{
#ifdef OS_Windows
    HWND window = argc > 0 ? window_handle_from_rvalue(&args[0]) : nullptr;
    set_console_bool(Result,
        window && IsWindowVisible(window) != FALSE);
#else
    set_console_bool(Result, false);
#endif
}

/// @description Opens an interactive console for a dedicated server.
/// @param {String} title Console window title.
/// @returns {Bool} Whether the console is available.
YYEXPORT void console_open(RValue& Result, CInstance*, CInstance*, int argc, RValue* args)
{
    const char* title = argc > 0 ? YYGetString(args, 0) : "";

    if (consoleActive)
    {
#ifdef OS_Windows
        const std::wstring wideTitle = utf8_to_wide(title);
        if (!wideTitle.empty())
            SetConsoleTitleW(wideTitle.c_str());
#else
        if (title && *title && isatty(STDOUT_FILENO))
        {
            std::fprintf(stdout, "\033]0;%s\007", title);
            std::fflush(stdout);
        }
#endif
        set_console_bool(Result, true);
        return;
    }

#ifdef OS_Windows
    consoleOwned = GetConsoleCP() == 0;
    if (consoleOwned && !AllocConsole())
    {
        consoleOwned = false;
        set_console_bool(Result, false);
        return;
    }

    consoleInput = CreateFileW(L"CONIN$", GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    consoleOutput = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);

    if (consoleInput == INVALID_HANDLE_VALUE || consoleOutput == INVALID_HANDLE_VALUE)
    {
        if (consoleInput != INVALID_HANDLE_VALUE)
            CloseHandle(consoleInput);
        if (consoleOutput != INVALID_HANDLE_VALUE)
            CloseHandle(consoleOutput);
        consoleInput = INVALID_HANDLE_VALUE;
        consoleOutput = INVALID_HANDLE_VALUE;
        if (consoleOwned)
            FreeConsole();
        consoleOwned = false;
        set_console_bool(Result, false);
        return;
    }

    consoleActive = true;
    consoleTextColor = -1;
    consoleBackgroundColor = -1;
    consolePromptVisible = false;

    CONSOLE_SCREEN_BUFFER_INFO bufferInfo{};
    if (GetConsoleScreenBufferInfo(consoleOutput, &bufferInfo))
        defaultConsoleAttributes = bufferInfo.wAttributes;
    consoleOutputModeSaved =
        GetConsoleMode(consoleOutput, &originalConsoleOutputMode) != FALSE;
    if (consoleOutputModeSaved)
    {
        const DWORD virtualTerminalMode = originalConsoleOutputMode |
            ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        consoleVirtualTerminal =
            SetConsoleMode(consoleOutput, virtualTerminalMode) != FALSE;
    }

    const std::wstring wideTitle = utf8_to_wide(title);
    if (!wideTitle.empty())
        SetConsoleTitleW(wideTitle.c_str());
#else
    consoleActive = stdout != nullptr;
    consoleTextColor = -1;
    consoleBackgroundColor = -1;
    consolePromptVisible = false;

    if (consoleActive && title && *title && isatty(STDOUT_FILENO))
    {
        std::fprintf(stdout, "\033]0;%s\007", title);
        std::fflush(stdout);
    }
#endif

    set_console_bool(Result, consoleActive);
}

/// @description Returns whether the dedicated-server console is open.
/// @returns {Bool}
YYEXPORT void console_is_open(RValue& Result, CInstance*, CInstance*, int, RValue*)
{
    set_console_bool(Result, consoleActive);
}

/// @description Writes one line to the dedicated-server console.
/// @param {String} text Text to write.
/// @returns {Bool} Whether the line was written.
YYEXPORT void console_write_line(RValue& Result, CInstance*, CInstance*, int argc, RValue* args)
{
    set_console_bool(Result,
        argc > 0 && write_console_line(YYGetString(args, 0)));
}

/// @description Writes one line to both GameMaker Output and the dedicated-server console.
/// @param {String} text Text to write.
/// @returns {Bool} Whether the line was written to the console.
YYEXPORT void console_log(RValue& Result, CInstance*, CInstance*, int argc, RValue* args)
{
    if (argc < 1)
    {
        set_console_bool(Result, false);
        return;
    }

    const char* text = YYGetString(args, 0);
    DebugConsoleOutput("%s\n", text ? text : "");
    set_console_bool(Result, write_console_line(text));
}

/// @description Clears the console display without discarding input or queued commands.
/// @returns {Bool}
YYEXPORT void console_clear(RValue& Result, CInstance*, CInstance*, int, RValue*)
{
    if (!consoleActive)
    {
        set_console_bool(Result, false);
        return;
    }

    const bool redrawPrompt = consolePromptVisible;
    consolePromptVisible = false;
    bool success = false;

#ifdef OS_Windows
    CONSOLE_SCREEN_BUFFER_INFO bufferInfo{};
    if (consoleOutput != INVALID_HANDLE_VALUE &&
        GetConsoleScreenBufferInfo(consoleOutput, &bufferInfo))
    {
        const COORD origin{ 0, 0 };
        const DWORD cellCount =
            static_cast<DWORD>(bufferInfo.dwSize.X) *
            static_cast<DWORD>(bufferInfo.dwSize.Y);
        DWORD charactersWritten = 0;
        DWORD attributesWritten = 0;
        success =
            FillConsoleOutputCharacterW(consoleOutput, L' ', cellCount,
                origin, &charactersWritten) != FALSE &&
            FillConsoleOutputAttribute(consoleOutput, bufferInfo.wAttributes,
                cellCount, origin, &attributesWritten) != FALSE &&
            SetConsoleCursorPosition(consoleOutput, origin) != FALSE;
    }
#else
    success = std::fwrite("\033[2J\033[3J\033[H", 1, 11, stdout) == 11;
    std::fflush(stdout);
#endif

    if (success && redrawPrompt)
        success = show_console_prompt();
    else if (!success)
        consolePromptVisible = redrawPrompt;
    set_console_bool(Result, success);
}

/// @description Changes the immutable prefix shown before console input.
/// @param {String} prompt Prompt text, such as "> " or "server> ".
/// @returns {Bool}
YYEXPORT void console_set_prompt(RValue& Result, CInstance*, CInstance*, int argc, RValue* args)
{
    if (argc < 1)
    {
        set_console_bool(Result, false);
        return;
    }

#ifdef OS_Windows
    clear_console_prompt_visual();
#else
    if (consoleActive && consolePromptVisible)
    {
        std::fwrite("\n", 1, 1, stdout);
        std::fflush(stdout);
        consolePromptVisible = false;
    }
#endif
    const char* prompt = YYGetString(args, 0);
    consolePrompt = prompt ? prompt : "";
    set_console_bool(Result, true);
}

/// @description Changes the color used by subsequent console text.
/// @param {Real} color A console_color_* constant.
/// @returns {Bool}
YYEXPORT void console_set_text_color(RValue& Result, CInstance*, CInstance*, int argc, RValue* args)
{
    set_console_bool(Result,
        argc > 0 && set_console_text_color_impl(YYGetInt32(args, 0)));
}

/// @description Changes the background color used by subsequent console text.
/// @param {Real} color A console_color_* constant.
/// @returns {Bool}
YYEXPORT void console_set_background_color(RValue& Result, CInstance*, CInstance*, int argc, RValue* args)
{
    set_console_bool(Result,
        argc > 0 && set_console_background_color_impl(YYGetInt32(args, 0)));
}

/// @description Restores the console's original text and background colors.
/// @returns {Bool}
YYEXPORT void console_reset_color(RValue& Result, CInstance*, CInstance*, int, RValue*)
{
    if (!consoleActive)
    {
        set_console_bool(Result, false);
        return;
    }

    const int previousText = consoleTextColor;
    const int previousBackground = consoleBackgroundColor;
    consoleTextColor = -1;
    consoleBackgroundColor = -1;
    if (!apply_console_colors())
    {
        consoleTextColor = previousText;
        consoleBackgroundColor = previousBackground;
        set_console_bool(Result, false);
        return;
    }
    set_console_bool(Result, true);
}

/// @description Writes one line in a temporary text color.
/// @param {String} text Text to write.
/// @param {Real} color A console_color_* constant.
/// @returns {Bool}
YYEXPORT void console_write_line_colored(RValue& Result, CInstance*, CInstance*, int argc, RValue* args)
{
    if (argc < 2 || !consoleActive)
    {
        set_console_bool(Result, false);
        return;
    }

    const int color = YYGetInt32(args, 1);
    if (color < 0 || color > 15)
    {
        set_console_bool(Result, false);
        return;
    }

    const int previousText = consoleTextColor;
    const bool colorChanged = set_console_text_color_impl(color);
    const bool lineWritten = colorChanged &&
        write_console_line(YYGetString(args, 0));
    consoleTextColor = previousText;
    const bool colorRestored = apply_console_colors();
    set_console_bool(Result,
        colorChanged && lineWritten && colorRestored);
}

/// @description Gets the next complete command without blocking the game loop.
/// @returns {String} Command text, or an empty string when none is available.
YYEXPORT void console_read_line(RValue& Result, CInstance*, CInstance*, int, RValue*)
{
    poll_console_input();
    if (consoleLines.empty())
    {
        YYCreateString(&Result, "");
        return;
    }

    const std::string line = consoleLines.front();
    consoleLines.pop_front();
    YYCreateString(&Result, line.c_str());
}

/// @description Changes the dedicated-server console title.
/// @param {String} title New title.
/// @returns {Bool} Whether the title was changed.
YYEXPORT void console_set_title(RValue& Result, CInstance*, CInstance*, int argc, RValue* args)
{
    if (!consoleActive || argc < 1)
    {
        set_console_bool(Result, false);
        return;
    }

    const char* title = YYGetString(args, 0);
#ifdef OS_Windows
    const std::wstring wideTitle = utf8_to_wide(title);
    set_console_bool(Result,
        (!title || !*title) ? SetConsoleTitleW(L"") != FALSE :
        !wideTitle.empty() && SetConsoleTitleW(wideTitle.c_str()) != FALSE);
#else
    if (!isatty(STDOUT_FILENO))
    {
        set_console_bool(Result, false);
        return;
    }
    std::fprintf(stdout, "\033]0;%s\007", title ? title : "");
    std::fflush(stdout);
    set_console_bool(Result, true);
#endif
}

/// @description Closes a console created by console_open.
/// @returns {Bool} Whether a console was open.
YYEXPORT void console_close(RValue& Result, CInstance*, CInstance*, int, RValue*)
{
    const bool wasActive = consoleActive;
    if (!consoleActive)
    {
        set_console_bool(Result, false);
        return;
    }

#ifdef OS_Windows
    clear_console_prompt_visual();
    consoleTextColor = -1;
    consoleBackgroundColor = -1;
    apply_console_colors();
    if (consoleOutputModeSaved && consoleOutput != INVALID_HANDLE_VALUE)
        SetConsoleMode(consoleOutput, originalConsoleOutputMode);
    if (consoleInput != INVALID_HANDLE_VALUE)
        CloseHandle(consoleInput);
    if (consoleOutput != INVALID_HANDLE_VALUE)
        CloseHandle(consoleOutput);
    consoleInput = INVALID_HANDLE_VALUE;
    consoleOutput = INVALID_HANDLE_VALUE;
    consoleInputLine.clear();
    consoleOutputModeSaved = false;
    consoleVirtualTerminal = false;
    if (consoleOwned)
        FreeConsole();
    consoleOwned = false;
#else
    if (consolePromptVisible)
        std::fwrite("\n", 1, 1, stdout);
    consolePromptVisible = false;
    consoleTextColor = -1;
    consoleBackgroundColor = -1;
    apply_console_colors();
    consoleInputBytes.clear();
#endif

    consoleActive = false;
    consolePromptVisible = false;
    consoleLines.clear();
    set_console_bool(Result, wasActive);
}
