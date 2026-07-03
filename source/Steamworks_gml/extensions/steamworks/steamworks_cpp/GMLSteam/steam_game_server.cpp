#include "pch.h"
#include "steam_api.h"
#include "steam_gameserver.h"
#include "Extension_Interface.h"
#include "YYRValue.h"
#include "steam_common.h"
#include "steam_glue.h"

#include <cstdio>

bool steam_game_server_is_initialised = false;

namespace {

ISteamGameServer* server()
{
    return steam_game_server_is_initialised ? SteamGameServer() : nullptr;
}

void set_bool(RValue& result, bool value)
{
    result.kind = VALUE_BOOL;
    result.val = value;
}

void set_real(RValue& result, double value)
{
    result.kind = VALUE_REAL;
    result.val = value;
}

bool is_numeric_rvalue(const RValue* value)
{
    if (!value)
        return false;
    const int kind = KIND_RValue(value);
    return kind == VALUE_REAL || kind == VALUE_INT32 ||
        kind == VALUE_INT64 || kind == VALUE_BOOL;
}

RValue* game_server_config_member(RValue* config, const char* name,
    bool required = true)
{
    RValue* member = YYStructGetMember(config, name);
    if (!member && required)
        DebugConsoleOutput(
            "steam_game_server_init: missing config.%s\n", name);
    return member;
}

void dispatch(const char* type)
{
    int map = CreateDsMap(0, 0);
    DsMapAddString(map, "event_type", type);
    CreateAsyncEventWithDSMap(map, EVENT_OTHER_WEB_STEAM);
}

class GameServerCallbacks
{
public:
    GameServerCallbacks()
    {
        reputation.SetGameserverFlag();
        associateClan.SetGameserverFlag();
        compatibility.SetGameserverFlag();
    }

    STEAM_GAMESERVER_CALLBACK(GameServerCallbacks, onConnected, SteamServersConnected_t);
    STEAM_GAMESERVER_CALLBACK(GameServerCallbacks, onConnectFailure, SteamServerConnectFailure_t);
    STEAM_GAMESERVER_CALLBACK(GameServerCallbacks, onDisconnected, SteamServersDisconnected_t);
    STEAM_GAMESERVER_CALLBACK(GameServerCallbacks, onPolicy, GSPolicyResponse_t);
    STEAM_GAMESERVER_CALLBACK(GameServerCallbacks, onAuth, ValidateAuthTicketResponse_t);
    STEAM_GAMESERVER_CALLBACK(GameServerCallbacks, onGroupStatus, GSClientGroupStatus_t);
    STEAM_GAMESERVER_CALLBACK(GameServerCallbacks, onStatsReceived, GSStatsReceived_t);
    STEAM_GAMESERVER_CALLBACK(GameServerCallbacks, onStatsStored, GSStatsStored_t);
    STEAM_GAMESERVER_CALLBACK(GameServerCallbacks, onStatsUnloaded, GSStatsUnloaded_t);

    CCallResult<GameServerCallbacks, GSReputation_t> reputation;
    CCallResult<GameServerCallbacks, AssociateWithClanResult_t> associateClan;
    CCallResult<GameServerCallbacks, ComputeNewPlayerCompatibilityResult_t> compatibility;

    void onReputation(GSReputation_t* value, bool ioFailure);
    void onAssociateClan(AssociateWithClanResult_t* value, bool ioFailure);
    void onCompatibility(ComputeNewPlayerCompatibilityResult_t* value, bool ioFailure);
};

GameServerCallbacks callbacks;

void GameServerCallbacks::onConnected(SteamServersConnected_t*)
{
    dispatch("steam_game_server_connected");
}

void GameServerCallbacks::onConnectFailure(SteamServerConnectFailure_t* value)
{
    int map = CreateDsMap(0, 0);
    DsMapAddString(map, "event_type", "steam_game_server_connect_failure");
    DsMapAddDouble(map, "result", value->m_eResult);
    DsMapAddBool(map, "still_retrying", value->m_bStillRetrying);
    CreateAsyncEventWithDSMap(map, EVENT_OTHER_WEB_STEAM);
}

void GameServerCallbacks::onDisconnected(SteamServersDisconnected_t* value)
{
    int map = CreateDsMap(0, 0);
    DsMapAddString(map, "event_type", "steam_game_server_disconnected");
    DsMapAddDouble(map, "result", value->m_eResult);
    CreateAsyncEventWithDSMap(map, EVENT_OTHER_WEB_STEAM);
}

void GameServerCallbacks::onPolicy(GSPolicyResponse_t* value)
{
    int map = CreateDsMap(0, 0);
    DsMapAddString(map, "event_type", "steam_game_server_policy_response");
    DsMapAddBool(map, "secure", value->m_bSecure != 0);
    CreateAsyncEventWithDSMap(map, EVENT_OTHER_WEB_STEAM);
}

void GameServerCallbacks::onAuth(ValidateAuthTicketResponse_t* value)
{
    int map = CreateDsMap(0, 0);
    DsMapAddString(map, "event_type", "steam_game_server_validate_auth_ticket_response");
    DsMapAddInt64(map, "steam_id", value->m_SteamID.ConvertToUint64());
    DsMapAddInt64(map, "owner_steam_id", value->m_OwnerSteamID.ConvertToUint64());
    DsMapAddDouble(map, "auth_session_response", value->m_eAuthSessionResponse);
    CreateAsyncEventWithDSMap(map, EVENT_OTHER_WEB_STEAM);
}

void GameServerCallbacks::onGroupStatus(GSClientGroupStatus_t* value)
{
    int map = CreateDsMap(0, 0);
    DsMapAddString(map, "event_type", "steam_game_server_user_group_status");
    DsMapAddInt64(map, "steam_id_user", value->m_SteamIDUser.ConvertToUint64());
    DsMapAddInt64(map, "steam_id_group", value->m_SteamIDGroup.ConvertToUint64());
    DsMapAddBool(map, "member", value->m_bMember);
    DsMapAddBool(map, "officer", value->m_bOfficer);
    CreateAsyncEventWithDSMap(map, EVENT_OTHER_WEB_STEAM);
}

void GameServerCallbacks::onStatsReceived(GSStatsReceived_t* value)
{
    int map = CreateDsMap(0, 0);
    DsMapAddString(map, "event_type", "steam_game_server_stats_received");
    DsMapAddBool(map, "success", value->m_eResult == k_EResultOK);
    DsMapAddDouble(map, "result", value->m_eResult);
    DsMapAddInt64(map, "steam_id_user", value->m_steamIDUser.ConvertToUint64());
    CreateAsyncEventWithDSMap(map, EVENT_OTHER_WEB_STEAM);
}

void GameServerCallbacks::onStatsStored(GSStatsStored_t* value)
{
    int map = CreateDsMap(0, 0);
    DsMapAddString(map, "event_type", "steam_game_server_stats_stored");
    DsMapAddBool(map, "success", value->m_eResult == k_EResultOK);
    DsMapAddDouble(map, "result", value->m_eResult);
    DsMapAddInt64(map, "steam_id_user", value->m_steamIDUser.ConvertToUint64());
    CreateAsyncEventWithDSMap(map, EVENT_OTHER_WEB_STEAM);
}

void GameServerCallbacks::onStatsUnloaded(GSStatsUnloaded_t* value)
{
    int map = CreateDsMap(0, 0);
    DsMapAddString(map, "event_type", "steam_game_server_stats_unloaded");
    DsMapAddInt64(map, "steam_id_user", value->m_steamIDUser.ConvertToUint64());
    CreateAsyncEventWithDSMap(map, EVENT_OTHER_WEB_STEAM);
}

void GameServerCallbacks::onReputation(GSReputation_t* value, bool ioFailure)
{
    int map = CreateDsMap(0, 0);
    DsMapAddString(map, "event_type", "steam_game_server_reputation");
    DsMapAddBool(map, "success", !ioFailure && value->m_eResult == k_EResultOK);
    DsMapAddDouble(map, "result", ioFailure ? k_EResultIOFailure : value->m_eResult);
    DsMapAddDouble(map, "reputation_score", value->m_unReputationScore);
    DsMapAddBool(map, "banned", value->m_bBanned);
    DsMapAddDouble(map, "banned_ip", value->m_unBannedIP);
    DsMapAddDouble(map, "banned_port", value->m_usBannedPort);
    DsMapAddInt64(map, "banned_game_id", value->m_ulBannedGameID);
    DsMapAddDouble(map, "ban_expires", value->m_unBanExpires);
    CreateAsyncEventWithDSMap(map, EVENT_OTHER_WEB_STEAM);
}

void GameServerCallbacks::onAssociateClan(AssociateWithClanResult_t* value, bool ioFailure)
{
    int map = CreateDsMap(0, 0);
    DsMapAddString(map, "event_type", "steam_game_server_associate_with_clan");
    DsMapAddBool(map, "success", !ioFailure && value->m_eResult == k_EResultOK);
    DsMapAddDouble(map, "result", ioFailure ? k_EResultIOFailure : value->m_eResult);
    CreateAsyncEventWithDSMap(map, EVENT_OTHER_WEB_STEAM);
}

void GameServerCallbacks::onCompatibility(ComputeNewPlayerCompatibilityResult_t* value, bool ioFailure)
{
    int map = CreateDsMap(0, 0);
    DsMapAddString(map, "event_type", "steam_game_server_compute_player_compatibility");
    DsMapAddBool(map, "success", !ioFailure && value->m_eResult == k_EResultOK);
    DsMapAddDouble(map, "result", ioFailure ? k_EResultIOFailure : value->m_eResult);
    DsMapAddDouble(map, "players_that_dont_like_candidate", value->m_cPlayersThatDontLikeCandidate);
    DsMapAddDouble(map, "players_candidate_doesnt_like", value->m_cPlayersThatCandidateDoesntLike);
    DsMapAddDouble(map, "clan_players_that_dont_like_candidate", value->m_cClanPlayersThatDontLikeCandidate);
    DsMapAddInt64(map, "candidate_steam_id", value->m_SteamIDCandidate.ConvertToUint64());
    CreateAsyncEventWithDSMap(map, EVENT_OTHER_WEB_STEAM);
}

}

YYEXPORT void steam_game_server_init(RValue& Result, CInstance*, CInstance*, int argc, RValue* args)
{
    if (steam_game_server_is_initialised) {
        set_bool(Result, true);
        return;
    }
    if (argc < 1 || KIND_RValue(args) != VALUE_OBJECT) {
        DebugConsoleOutput(
            "steam_game_server_init: expected one config struct\n");
        set_bool(Result, false);
        return;
    }

    RValue* config = YYGetStruct(args, 0);
    if (!config) {
        DebugConsoleOutput(
            "steam_game_server_init: config is not a struct\n");
        set_bool(Result, false);
        return;
    }

    RValue* ipValue = game_server_config_member(config, "ip", false);
    RValue* gamePortValue = game_server_config_member(config, "game_port");
    RValue* queryPortValue = game_server_config_member(config, "query_port");
    RValue* modeValue = game_server_config_member(config, "server_mode");
    RValue* versionValue = game_server_config_member(config, "version");
    if (!gamePortValue || !queryPortValue || !modeValue || !versionValue) {
        set_bool(Result, false);
        return;
    }
    if ((ipValue && !is_numeric_rvalue(ipValue)) ||
        !is_numeric_rvalue(gamePortValue) ||
        !is_numeric_rvalue(queryPortValue) ||
        !is_numeric_rvalue(modeValue) ||
        KIND_RValue(versionValue) != VALUE_STRING) {
        DebugConsoleOutput(
            "steam_game_server_init: invalid config member type\n");
        set_bool(Result, false);
        return;
    }

    const int64 ipNumber = ipValue ? YYGetInt64(ipValue, 0) : 0;
    const int gamePort = YYGetInt32(gamePortValue, 0);
    const int queryPort = YYGetInt32(queryPortValue, 0);
    const int mode = YYGetInt32(modeValue, 0);
    const char* version = versionValue->GetString();
    if (ipNumber < 0 || ipNumber > 0xFFFFFFFFLL ||
        gamePort < 0 || gamePort > 65535 ||
        queryPort < 0 || queryPort > 65535 ||
        mode < eServerModeNoAuthentication ||
        mode > eServerModeAuthenticationAndSecure ||
        !version || !*version) {
        DebugConsoleOutput(
            "steam_game_server_init: invalid config member value\n");
        set_bool(Result, false);
        return;
    }

    const uint32 ip = static_cast<uint32>(ipNumber);
    SteamErrMsg error{};
    ESteamAPIInitResult initResult = SteamGameServer_InitEx(
        ip, static_cast<uint16>(gamePort), static_cast<uint16>(queryPort),
        static_cast<EServerMode>(mode), version, &error);
    steam_game_server_is_initialised = initResult == k_ESteamAPIInitResult_OK;
    if (!steam_game_server_is_initialised)
        DebugConsoleOutput("SteamGameServer_InitEx failed: %s\n", error);
    set_bool(Result, steam_game_server_is_initialised);
}

YYEXPORT void steam_game_server_shutdown(RValue& Result, CInstance*, CInstance*, int, RValue*)
{
    if (steam_game_server_is_initialised) {
        callbacks.reputation.Cancel();
        callbacks.associateClan.Cancel();
        callbacks.compatibility.Cancel();
        SteamGameServer_Shutdown();
        steam_game_server_is_initialised = false;
    }
    set_bool(Result, true);
}

YYEXPORT void steam_game_server_update(RValue& Result, CInstance*, CInstance*, int, RValue*)
{
    if (steam_game_server_is_initialised)
        SteamGameServer_RunCallbacks();
    set_bool(Result, steam_game_server_is_initialised);
}

YYEXPORT void steam_game_server_initialised(RValue& Result, CInstance*, CInstance*, int, RValue*) { set_bool(Result, steam_game_server_is_initialised); }
YYEXPORT void steam_game_server_log_on(RValue& Result, CInstance*, CInstance*, int, RValue* args) { if (server()) server()->LogOn(YYGetString(args, 0)); set_bool(Result, server() != nullptr); }
YYEXPORT void steam_game_server_log_on_anonymous(RValue& Result, CInstance*, CInstance*, int, RValue*) { if (server()) server()->LogOnAnonymous(); set_bool(Result, server() != nullptr); }
YYEXPORT void steam_game_server_log_off(RValue& Result, CInstance*, CInstance*, int, RValue*) { if (server()) server()->LogOff(); set_bool(Result, server() != nullptr); }
YYEXPORT void steam_game_server_logged_on(RValue& Result, CInstance*, CInstance*, int, RValue*) { set_bool(Result, server() && server()->BLoggedOn()); }
YYEXPORT void steam_game_server_secure(RValue& Result, CInstance*, CInstance*, int, RValue*) { set_bool(Result, server() && server()->BSecure()); }
YYEXPORT void steam_game_server_get_steam_id(RValue& Result, CInstance*, CInstance*, int, RValue*) { Result.kind = VALUE_INT64; Result.v64 = server() ? server()->GetSteamID().ConvertToUint64() : 0; }
YYEXPORT void steam_game_server_was_restart_requested(RValue& Result, CInstance*, CInstance*, int, RValue*) { set_bool(Result, server() && server()->WasRestartRequested()); }

#define SERVER_STRING_SETTER(gmlName, method) \
YYEXPORT void gmlName(RValue& Result, CInstance*, CInstance*, int, RValue* args) { if (server()) server()->method(YYGetString(args, 0)); set_bool(Result, server() != nullptr); }
#define SERVER_INT_SETTER(gmlName, method) \
YYEXPORT void gmlName(RValue& Result, CInstance*, CInstance*, int, RValue* args) { if (server()) server()->method(YYGetInt32(args, 0)); set_bool(Result, server() != nullptr); }
#define SERVER_BOOL_SETTER(gmlName, method) \
YYEXPORT void gmlName(RValue& Result, CInstance*, CInstance*, int, RValue* args) { if (server()) server()->method(YYGetBool(args, 0)); set_bool(Result, server() != nullptr); }

SERVER_STRING_SETTER(steam_game_server_set_product, SetProduct)
SERVER_STRING_SETTER(steam_game_server_set_game_description, SetGameDescription)
SERVER_STRING_SETTER(steam_game_server_set_mod_dir, SetModDir)
SERVER_BOOL_SETTER(steam_game_server_set_dedicated, SetDedicatedServer)
SERVER_INT_SETTER(steam_game_server_set_max_player_count, SetMaxPlayerCount)
SERVER_INT_SETTER(steam_game_server_set_bot_player_count, SetBotPlayerCount)
SERVER_STRING_SETTER(steam_game_server_set_server_name, SetServerName)
SERVER_STRING_SETTER(steam_game_server_set_map_name, SetMapName)
SERVER_BOOL_SETTER(steam_game_server_set_password_protected, SetPasswordProtected)
SERVER_INT_SETTER(steam_game_server_set_spectator_port, SetSpectatorPort)
SERVER_STRING_SETTER(steam_game_server_set_spectator_server_name, SetSpectatorServerName)
SERVER_STRING_SETTER(steam_game_server_set_game_tags, SetGameTags)
SERVER_STRING_SETTER(steam_game_server_set_game_data, SetGameData)
SERVER_STRING_SETTER(steam_game_server_set_region, SetRegion)
SERVER_BOOL_SETTER(steam_game_server_set_advertise_server_active, SetAdvertiseServerActive)

YYEXPORT void steam_game_server_clear_all_key_values(RValue& Result, CInstance*, CInstance*, int, RValue*) { if (server()) server()->ClearAllKeyValues(); set_bool(Result, server() != nullptr); }
YYEXPORT void steam_game_server_set_key_value(RValue& Result, CInstance*, CInstance*, int, RValue* args) { if (server()) server()->SetKeyValue(YYGetString(args, 0), YYGetString(args, 1)); set_bool(Result, server() != nullptr); }

YYEXPORT void steam_game_server_get_auth_session_ticket(RValue& Result, CInstance*, CInstance*, int, RValue* args)
{
    const int buffer = YYGetInt32(args, 0);
    const int suppliedMaxSize = YYGetInt32(args, 1);
    const int maxSize = suppliedMaxSize > 0 ? suppliedMaxSize : 0;
    bool success = false;
    HAuthTicket handle = k_HAuthTicketInvalid;
    uint32 ticketSize = 0;
    if (server() && BufferGetFromGML(buffer) && maxSize > 0) {
        uint8* bytes = new uint8[maxSize];
        handle = server()->GetAuthSessionTicket(bytes, maxSize, &ticketSize, nullptr);
        success = handle != k_HAuthTicketInvalid && ticketSize <= static_cast<uint32>(maxSize) &&
            BufferWriteContent(buffer, 0, bytes, ticketSize, true) == static_cast<int>(ticketSize);
        delete[] bytes;
    }
    YYStructCreate(&Result);
    YYStructAddBool(&Result, "success", success);
    YYStructAddInt(&Result, "ticket_handle", handle);
    YYStructAddInt(&Result, "ticket_size", success ? ticketSize : 0);
}

YYEXPORT void steam_game_server_begin_auth_session(RValue& Result, CInstance*, CInstance*, int, RValue* args)
{
    void* bytes = nullptr;
    int bufferSize = 0;
    if (!server() || !BufferGetContent(YYGetInt32(args, 0), &bytes, &bufferSize) || !bytes) {
        set_real(Result, k_EBeginAuthSessionResultInvalidTicket);
        return;
    }
    const int suppliedSize = YYGetInt32(args, 1);
    const int requested = suppliedSize > 0 ? suppliedSize : 0;
    const int ticketSize = bufferSize < requested ? bufferSize : requested;
    CSteamID steamID(static_cast<uint64>(YYGetInt64(args, 2)));
    EBeginAuthSessionResult authResult = server()->BeginAuthSession(bytes, ticketSize, steamID);
    YYFree(bytes);
    set_real(Result, authResult);
}

YYEXPORT void steam_game_server_end_auth_session(RValue& Result, CInstance*, CInstance*, int, RValue* args) { if (server()) server()->EndAuthSession(CSteamID(static_cast<uint64>(YYGetInt64(args, 0)))); set_bool(Result, server() != nullptr); }
YYEXPORT void steam_game_server_cancel_auth_ticket(RValue& Result, CInstance*, CInstance*, int, RValue* args) { if (server()) server()->CancelAuthTicket(static_cast<HAuthTicket>(YYGetInt32(args, 0))); set_bool(Result, server() != nullptr); }
YYEXPORT void steam_game_server_user_has_license_for_app(RValue& Result, CInstance*, CInstance*, int, RValue* args) { set_real(Result, server() ? server()->UserHasLicenseForApp(CSteamID(static_cast<uint64>(YYGetInt64(args, 0))), static_cast<AppId_t>(YYGetInt32(args, 1))) : k_EUserHasLicenseResultNoAuth); }
YYEXPORT void steam_game_server_request_user_group_status(RValue& Result, CInstance*, CInstance*, int, RValue* args) { set_bool(Result, server() && server()->RequestUserGroupStatus(CSteamID(static_cast<uint64>(YYGetInt64(args, 0))), CSteamID(static_cast<uint64>(YYGetInt64(args, 1))))); }

YYEXPORT void steam_game_server_get_public_ip(RValue& Result, CInstance*, CInstance*, int, RValue*)
{
    YYStructCreate(&Result);
    SteamIPAddress_t ip = SteamIPAddress_t::IPv4Any();
    if (server()) ip = server()->GetPublicIP();
    YYStructAddBool(&Result, "is_set", ip.IsSet());
    YYStructAddInt(&Result, "type", ip.m_eType);
    YYStructAddDouble(&Result, "ipv4", ip.m_eType == k_ESteamIPTypeIPv4 ? ip.m_unIPv4 : 0);
    char text[64] = {};
    if (ip.m_eType == k_ESteamIPTypeIPv4)
        std::snprintf(text, sizeof(text), "%u.%u.%u.%u", (ip.m_unIPv4 >> 24) & 255, (ip.m_unIPv4 >> 16) & 255, (ip.m_unIPv4 >> 8) & 255, ip.m_unIPv4 & 255);
    YYStructAddString(&Result, "address", text);
}

YYEXPORT void steam_game_server_handle_incoming_packet(RValue& Result, CInstance*, CInstance*, int, RValue* args)
{
    void* bytes = nullptr;
    int bufferSize = 0;
    if (!server() || !BufferGetContent(YYGetInt32(args, 0), &bytes, &bufferSize) || !bytes) {
        set_bool(Result, false);
        return;
    }
    const int suppliedSize = YYGetInt32(args, 1);
    const int requested = suppliedSize > 0 ? suppliedSize : 0;
    const int packetSize = bufferSize < requested ? bufferSize : requested;
    const bool handled = server()->HandleIncomingPacket(bytes, packetSize, static_cast<uint32>(YYGetInt64(args, 2)), static_cast<uint16>(YYGetInt32(args, 3)));
    YYFree(bytes);
    set_bool(Result, handled);
}

YYEXPORT void steam_game_server_get_next_outgoing_packet(RValue& Result, CInstance*, CInstance*, int, RValue* args)
{
    const int buffer = YYGetInt32(args, 0);
    const int suppliedMaxSize = YYGetInt32(args, 1);
    const int maxSize = suppliedMaxSize > 0 ? suppliedMaxSize : 0;
    uint32 ip = 0;
    uint16 port = 0;
    int size = 0;
    if (server() && BufferGetFromGML(buffer) && maxSize > 0) {
        uint8* bytes = new uint8[maxSize];
        size = server()->GetNextOutgoingPacket(bytes, maxSize, &ip, &port);
        if (size > 0 && BufferWriteContent(buffer, 0, bytes, size, true) != size)
            size = 0;
        delete[] bytes;
    }
    YYStructCreate(&Result);
    YYStructAddInt(&Result, "size", size);
    YYStructAddDouble(&Result, "ip", ip);
    YYStructAddInt(&Result, "port", port);
}

YYEXPORT void steam_game_server_associate_with_clan(RValue& Result, CInstance*, CInstance*, int, RValue* args)
{
    if (!server()) { set_bool(Result, false); return; }
    SteamAPICall_t call = server()->AssociateWithClan(CSteamID(static_cast<uint64>(YYGetInt64(args, 0))));
    callbacks.associateClan.Set(call, &callbacks, &GameServerCallbacks::onAssociateClan);
    set_bool(Result, call != k_uAPICallInvalid);
}

YYEXPORT void steam_game_server_compute_new_player_compatibility(RValue& Result, CInstance*, CInstance*, int, RValue* args)
{
    if (!server()) { set_bool(Result, false); return; }
    SteamAPICall_t call = server()->ComputeNewPlayerCompatibility(CSteamID(static_cast<uint64>(YYGetInt64(args, 0))));
    callbacks.compatibility.Set(call, &callbacks, &GameServerCallbacks::onCompatibility);
    set_bool(Result, call != k_uAPICallInvalid);
}

YYEXPORT void steam_game_server_get_server_reputation(RValue& Result, CInstance*, CInstance*, int, RValue*)
{
    if (!server()) { set_bool(Result, false); return; }
    SteamAPICall_t call = server()->GetServerReputation();
    callbacks.reputation.Set(call, &callbacks, &GameServerCallbacks::onReputation);
    set_bool(Result, call != k_uAPICallInvalid);
}

YYEXPORT void steam_game_server_create_unauthenticated_user_connection(RValue& Result, CInstance*, CInstance*, int, RValue*) { Result.kind = VALUE_INT64; Result.v64 = server() ? server()->CreateUnauthenticatedUserConnection().ConvertToUint64() : 0; }
YYEXPORT void steam_game_server_update_user_data(RValue& Result, CInstance*, CInstance*, int, RValue* args) { set_bool(Result, server() && server()->BUpdateUserData(CSteamID(static_cast<uint64>(YYGetInt64(args, 0))), YYGetString(args, 1), static_cast<uint32>(YYGetInt64(args, 2)))); }

YYEXPORT void steam_game_server_stats_request_user_stats(RValue& Result, CInstance*, CInstance*, int, RValue* args)
{
    Result.kind = VALUE_INT64;
    Result.v64 = steam_game_server_is_initialised && SteamGameServerStats()
        ? SteamGameServerStats()->RequestUserStats(CSteamID(static_cast<uint64>(YYGetInt64(args, 0))))
        : k_uAPICallInvalid;
}

YYEXPORT void steam_game_server_stats_get_user_stat_int(RValue& Result, CInstance*, CInstance*, int, RValue* args)
{
    int32 value = 0;
    bool success = steam_game_server_is_initialised && SteamGameServerStats() &&
        SteamGameServerStats()->GetUserStat(CSteamID(static_cast<uint64>(YYGetInt64(args, 0))), YYGetString(args, 1), &value);
    YYStructCreate(&Result);
    YYStructAddBool(&Result, "success", success);
    YYStructAddInt(&Result, "value", value);
}

YYEXPORT void steam_game_server_stats_get_user_stat_float(RValue& Result, CInstance*, CInstance*, int, RValue* args)
{
    float value = 0;
    bool success = steam_game_server_is_initialised && SteamGameServerStats() &&
        SteamGameServerStats()->GetUserStat(CSteamID(static_cast<uint64>(YYGetInt64(args, 0))), YYGetString(args, 1), &value);
    YYStructCreate(&Result);
    YYStructAddBool(&Result, "success", success);
    YYStructAddDouble(&Result, "value", value);
}

YYEXPORT void steam_game_server_stats_get_user_achievement(RValue& Result, CInstance*, CInstance*, int, RValue* args)
{
    bool achieved = false;
    bool success = steam_game_server_is_initialised && SteamGameServerStats() &&
        SteamGameServerStats()->GetUserAchievement(CSteamID(static_cast<uint64>(YYGetInt64(args, 0))), YYGetString(args, 1), &achieved);
    YYStructCreate(&Result);
    YYStructAddBool(&Result, "success", success);
    YYStructAddBool(&Result, "achieved", achieved);
}

YYEXPORT void steam_game_server_stats_set_user_stat_int(RValue& Result, CInstance*, CInstance*, int, RValue* args)
{
    set_bool(Result, steam_game_server_is_initialised && SteamGameServerStats() &&
        SteamGameServerStats()->SetUserStat(CSteamID(static_cast<uint64>(YYGetInt64(args, 0))), YYGetString(args, 1), YYGetInt32(args, 2)));
}

YYEXPORT void steam_game_server_stats_set_user_stat_float(RValue& Result, CInstance*, CInstance*, int, RValue* args)
{
    set_bool(Result, steam_game_server_is_initialised && SteamGameServerStats() &&
        SteamGameServerStats()->SetUserStat(CSteamID(static_cast<uint64>(YYGetInt64(args, 0))), YYGetString(args, 1), static_cast<float>(YYGetReal(args, 2))));
}

YYEXPORT void steam_game_server_stats_update_user_avg_rate_stat(RValue& Result, CInstance*, CInstance*, int, RValue* args)
{
    set_bool(Result, steam_game_server_is_initialised && SteamGameServerStats() &&
        SteamGameServerStats()->UpdateUserAvgRateStat(CSteamID(static_cast<uint64>(YYGetInt64(args, 0))), YYGetString(args, 1), static_cast<float>(YYGetReal(args, 2)), YYGetReal(args, 3)));
}

YYEXPORT void steam_game_server_stats_set_user_achievement(RValue& Result, CInstance*, CInstance*, int, RValue* args)
{
    set_bool(Result, steam_game_server_is_initialised && SteamGameServerStats() &&
        SteamGameServerStats()->SetUserAchievement(CSteamID(static_cast<uint64>(YYGetInt64(args, 0))), YYGetString(args, 1)));
}

YYEXPORT void steam_game_server_stats_clear_user_achievement(RValue& Result, CInstance*, CInstance*, int, RValue* args)
{
    set_bool(Result, steam_game_server_is_initialised && SteamGameServerStats() &&
        SteamGameServerStats()->ClearUserAchievement(CSteamID(static_cast<uint64>(YYGetInt64(args, 0))), YYGetString(args, 1)));
}

YYEXPORT void steam_game_server_stats_store_user_stats(RValue& Result, CInstance*, CInstance*, int, RValue* args)
{
    Result.kind = VALUE_INT64;
    Result.v64 = steam_game_server_is_initialised && SteamGameServerStats()
        ? SteamGameServerStats()->StoreUserStats(CSteamID(static_cast<uint64>(YYGetInt64(args, 0))))
        : k_uAPICallInvalid;
}
