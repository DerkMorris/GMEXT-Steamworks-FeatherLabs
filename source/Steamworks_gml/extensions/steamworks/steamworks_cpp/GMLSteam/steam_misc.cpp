/// steam_misc.cpp
#include "pch.h"
#include "steam_glue.h"
#include "steam_api.h"
#include "Extension_Interface.h"
#include "YYRValue.h"
#include "steam_common.h"

#include <cstdio>
#include <string>
#include <vector>

#ifdef OS_Windows
#include <winsock2.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
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
