/// steam_networking.cpp

#include "pch.h"
#include "steam_glue.h"
#include "steam_api.h"
#include "steam_gameserver.h"
#include "Extension_Interface.h"
#include "YYRValue.h"
#include "steam_common.h"

#ifndef STEAM_NET_ENABLE_P2P_SEND_DIAGNOSTICS
#define STEAM_NET_ENABLE_P2P_SEND_DIAGNOSTICS 0
#endif

namespace {

/// Basic UDP send (<1200 bytes; may get lost)
#define steam_net_packet_type_unreliable 0
/// Instant non-buffering UDP send (e.g. for voice data)
#define steam_net_packet_type_unreliable_nodelay 1
/// Reliable send (up to 1MB)
#define steam_net_packet_type_reliable 2
/// Buffering send (Nagle algorithm)
#define steam_net_packet_type_reliable_buffer 3

struct steam_p2p_packet_state_t
{
	uint32 size = 0;
	void* data = nullptr;
	CSteamID sender;
	EP2PSend packet_type = k_EP2PSendReliable;
	int send_channel = 0;
};

struct steam_p2p_send_diagnostic_t
{
#if STEAM_NET_ENABLE_P2P_SEND_DIAGNOSTICS
	bool was_called = false;
	bool is_sent = false;
	bool is_steam_initialised = false;
	bool is_steam_running = false;
	bool is_client_logged_on = false;
	bool is_game_server_initialised = false;
	bool is_networking_available = false;
	bool is_target_valid = false;
	bool is_buffer_available = false;
	uint64 local_client_id = 0;
	uint64 local_game_server_id = 0;
	uint64 target_id = 0;
	int target_arg_kind = VALUE_UNDEFINED;
	int target_account_type = 0;
	int target_universe = 0;
	int requested_size = 0;
	int buffer_size = 0;
	int packet_type = 0;
	int channel = 0;
#endif
};

bool steam_net_client_auto_accept_p2p_sessions = true;
bool steam_net_game_server_auto_accept_p2p_sessions = false;
steam_p2p_packet_state_t steam_net_client_packet_state;
steam_p2p_packet_state_t steam_net_game_server_packet_state;
steam_p2p_send_diagnostic_t steam_net_client_send_diagnostic;
steam_p2p_send_diagnostic_t steam_net_game_server_send_diagnostic;

ISteamNetworking* steam_net_client_networking()
{
	return steam_is_initialised ? SteamNetworking() : nullptr;
}

ISteamNetworking* steam_net_game_server_networking()
{
	return steam_game_server_is_initialised ? SteamGameServerNetworking() : nullptr;
}

void steam_net_set_bool_result(RValue& result, bool value)
{
	result.kind = VALUE_BOOL;
	result.val = value;
}

void steam_net_set_real_result(RValue& result, double value)
{
	result.kind = VALUE_REAL;
	result.val = value;
}

void steam_net_dispatch_p2p_event(const char* event_type, CSteamID remote_id)
{
	steam_net_event event((char*)event_type);
	event.set_steamid_all("user_id", remote_id);
	event.dispatch();
}

void steam_net_set_auto_accept_p2p_sessions_impl(RValue& result, RValue* args, bool& is_auto_accept_enabled)
{
	is_auto_accept_enabled = YYGetBool(args, 0);
	steam_net_set_bool_result(result, true);
}

void steam_net_accept_p2p_session_impl(RValue& result, RValue* args, ISteamNetworking* networking)
{
	CSteamID user(static_cast<uint64>(YYGetInt64(args, 0)));
	steam_net_set_bool_result(result, networking && networking->AcceptP2PSessionWithUser(user));
}

void steam_net_close_p2p_session_impl(RValue& result, RValue* args, ISteamNetworking* networking)
{
	CSteamID user(static_cast<uint64>(YYGetInt64(args, 0)));
	steam_net_set_bool_result(result, networking && networking->CloseP2PSessionWithUser(user));
}

void steam_net_packet_set_type_impl(RValue& result, RValue* args, steam_p2p_packet_state_t& state)
{
	EP2PSend packet_type = k_EP2PSendUnreliable;
	switch (YYGetInt32(args, 0)) {
		case steam_net_packet_type_unreliable_nodelay:
			packet_type = k_EP2PSendUnreliableNoDelay;
		break;

		case steam_net_packet_type_reliable:
			packet_type = k_EP2PSendReliable;
		break;

		case steam_net_packet_type_reliable_buffer:
			packet_type = k_EP2PSendReliableWithBuffering;
		break;
	}

	state.packet_type = packet_type;
	steam_net_set_bool_result(result, true);
}

void steam_net_packet_set_send_options_impl(RValue& result, RValue* args, steam_p2p_packet_state_t& state)
{
	steam_net_packet_set_type_impl(result, args, state);
	state.send_channel = YYGetInt32(args, 1);
}

void steam_net_packet_send_impl(
	RValue& result,
	int argc,
	RValue* args,
	ISteamNetworking* networking,
	steam_p2p_packet_state_t& state,
	steam_p2p_send_diagnostic_t& diagnostic,
	const char* backend_name)
{
#if STEAM_NET_ENABLE_P2P_SEND_DIAGNOSTICS
	diagnostic = {};
	diagnostic.was_called = true;
	diagnostic.is_steam_initialised = steam_is_initialised;
	diagnostic.is_game_server_initialised = steam_game_server_is_initialised;
	diagnostic.is_networking_available = networking != nullptr;
	diagnostic.target_arg_kind = KIND_RValue(args);
#else
	(void)diagnostic;
	(void)backend_name;
#endif

	uint64 target_id = static_cast<uint64>(YYGetInt64(args, 0));
	int32 buffer_idx = YYGetInt32(args, 1);
	int32 size = argc > 2 ? YYGetInt32(args, 2) : -1;
#if STEAM_NET_ENABLE_P2P_SEND_DIAGNOSTICS
	diagnostic.is_steam_running = SteamAPI_IsSteamRunning();
	diagnostic.target_id = target_id;
	diagnostic.requested_size = size;

	ISteamUser* steam_user = steam_is_initialised ? SteamUser() : nullptr;
	diagnostic.is_client_logged_on = steam_user && steam_user->BLoggedOn();
	diagnostic.local_client_id = steam_user ? steam_user->GetSteamID().ConvertToUint64() : 0;
	diagnostic.local_game_server_id = steam_game_server_is_initialised && SteamGameServer()
		? SteamGameServer()->GetSteamID().ConvertToUint64()
		: 0;
#endif

	CSteamID target(target_id);
#if STEAM_NET_ENABLE_P2P_SEND_DIAGNOSTICS
	diagnostic.is_target_valid = target.IsValid();
	diagnostic.target_account_type = static_cast<int>(target.GetEAccountType());
	diagnostic.target_universe = static_cast<int>(target.GetEUniverse());
#endif

	EP2PSend packet_type = state.packet_type;

	void* buffer_data = nullptr;
	int buffer_size = 0;
	if (!BufferGetContent(buffer_idx, &buffer_data, &buffer_size) || !buffer_data) {
		DebugConsoleOutput("steam_net_packet_send() - error: specified buffer %d not found\n", static_cast<int>(buffer_idx));
		steam_net_set_bool_result(result, false);
		return;
	}
#if STEAM_NET_ENABLE_P2P_SEND_DIAGNOSTICS
	diagnostic.is_buffer_available = true;
	diagnostic.buffer_size = buffer_size;
#endif

	if (size <= -1 || size > buffer_size) {
		size = buffer_size;
	}

	int steam_channel = state.send_channel;
#if STEAM_NET_ENABLE_P2P_SEND_DIAGNOSTICS
	diagnostic.requested_size = size;
	diagnostic.packet_type = static_cast<int>(packet_type);
	diagnostic.channel = steam_channel;
#endif
	bool is_sent = networking && networking->SendP2PPacket(target, buffer_data, size, packet_type, steam_channel);
#if STEAM_NET_ENABLE_P2P_SEND_DIAGNOSTICS
	diagnostic.is_sent = is_sent;
#endif
	steam_net_set_bool_result(result, is_sent);
#if STEAM_NET_ENABLE_P2P_SEND_DIAGNOSTICS
	if (!is_sent) {
		DebugConsoleOutput(
			"[steam_net_packet_send][%s] failed: steam_is_initialised=%d; steam_is_running=%d; client_logged_on=%d; "
			"game_server_is_initialised=%d; networking_available=%d; local_client_id=%llu; local_game_server_id=%llu; "
			"target_id=%llu; target_is_valid=%d; target_account_type=%d; target_universe=%d; "
			"requested_size=%d; buffer_size=%d; packet_type=%d; channel=%d\n",
			backend_name,
			steam_is_initialised ? 1 : 0,
			SteamAPI_IsSteamRunning() ? 1 : 0,
			steam_user && steam_user->BLoggedOn() ? 1 : 0,
			steam_game_server_is_initialised ? 1 : 0,
			networking ? 1 : 0,
			static_cast<unsigned long long>(diagnostic.local_client_id),
			static_cast<unsigned long long>(diagnostic.local_game_server_id),
			static_cast<unsigned long long>(target_id),
			target.IsValid() ? 1 : 0,
			static_cast<int>(target.GetEAccountType()),
			static_cast<int>(target.GetEUniverse()),
			size,
			buffer_size,
			static_cast<int>(packet_type),
			steam_channel
		);
	}
#endif
	YYFree(buffer_data);
}

void steam_net_packet_get_last_send_diagnostic_impl(RValue& result, const steam_p2p_send_diagnostic_t& diagnostic)
{
	YYStructCreate(&result);
#if STEAM_NET_ENABLE_P2P_SEND_DIAGNOSTICS
	YYStructAddBool(&result, "was_called", diagnostic.was_called);
	YYStructAddBool(&result, "is_sent", diagnostic.is_sent);
	YYStructAddBool(&result, "is_steam_initialised", diagnostic.is_steam_initialised);
	YYStructAddBool(&result, "is_steam_running", diagnostic.is_steam_running);
	YYStructAddBool(&result, "is_client_logged_on", diagnostic.is_client_logged_on);
	YYStructAddBool(&result, "is_game_server_initialised", diagnostic.is_game_server_initialised);
	YYStructAddBool(&result, "is_networking_available", diagnostic.is_networking_available);
	YYStructAddBool(&result, "is_target_valid", diagnostic.is_target_valid);
	YYStructAddBool(&result, "is_buffer_available", diagnostic.is_buffer_available);
	YYStructAddInt64(&result, "local_client_id", diagnostic.local_client_id);
	YYStructAddInt64(&result, "local_game_server_id", diagnostic.local_game_server_id);
	YYStructAddInt64(&result, "target_id", diagnostic.target_id);
	YYStructAddInt(&result, "target_arg_kind", diagnostic.target_arg_kind);
	YYStructAddInt(&result, "target_account_type", diagnostic.target_account_type);
	YYStructAddInt(&result, "target_universe", diagnostic.target_universe);
	YYStructAddInt(&result, "requested_size", diagnostic.requested_size);
	YYStructAddInt(&result, "buffer_size", diagnostic.buffer_size);
	YYStructAddInt(&result, "packet_type", diagnostic.packet_type);
	YYStructAddInt(&result, "channel", diagnostic.channel);
#else
	(void)diagnostic;
#endif
}

void steam_net_packet_receive_impl(
	RValue& result,
	int argc,
	RValue* args,
	ISteamNetworking* networking,
	steam_p2p_packet_state_t& state)
{
	int steam_channel = argc > 0 ? YYGetInt32(args, 0) : 0;
	uint32 available_size = 0;
	if (!networking || !networking->IsP2PPacketAvailable(&available_size, steam_channel)) {
		steam_net_set_bool_result(result, false);
		return;
	}

	if (state.data != nullptr) {
		free(state.data);
		state.data = nullptr;
	}

	state.data = malloc(available_size);
	if (networking->ReadP2PPacket(state.data, available_size, &state.size, &state.sender, steam_channel)) {
		steam_net_set_bool_result(result, true);
		return;
	}

	free(state.data);
	state.data = nullptr;
	state.size = 0;
	steam_net_set_bool_result(result, false);
}

void steam_net_packet_get_size_impl(RValue& result, const steam_p2p_packet_state_t& state)
{
	steam_net_set_real_result(result, state.size);
}

void steam_net_packet_get_data_impl(RValue& result, RValue* args, const steam_p2p_packet_state_t& state)
{
	int32 buffer_idx = YYGetInt32(args, 0);
	if (state.data == nullptr) {
		steam_net_set_bool_result(result, false);
		return;
	}

	steam_net_set_bool_result(
		result,
		BufferWriteContent(buffer_idx, 0, state.data, state.size, true) == static_cast<int>(state.size)
	);
}

void steam_net_packet_get_sender_id_impl(RValue& result, const steam_p2p_packet_state_t& state)
{
	result.kind = VALUE_INT64;
	result.v64 = state.sender.ConvertToUint64();
}

class steam_game_server_net_callbacks_t
{
public:
	STEAM_GAMESERVER_CALLBACK(steam_game_server_net_callbacks_t, p2p_session_request, P2PSessionRequest_t);
	STEAM_GAMESERVER_CALLBACK(steam_game_server_net_callbacks_t, p2p_session_connect_fail, P2PSessionConnectFail_t);
};

steam_game_server_net_callbacks_t steam_game_server_net_callbacks;

void steam_game_server_net_callbacks_t::p2p_session_request(P2PSessionRequest_t* event)
{
	CSteamID remote_id = event->m_steamIDRemote;
	steam_net_dispatch_p2p_event("game_server_p2p_session_request", remote_id);

	ISteamNetworking* networking = steam_net_game_server_networking();
	if (steam_net_game_server_auto_accept_p2p_sessions && networking) {
		networking->AcceptP2PSessionWithUser(remote_id);
	}
}

void steam_game_server_net_callbacks_t::p2p_session_connect_fail(P2PSessionConnectFail_t* event)
{
	steam_net_event async_event((char*)"game_server_p2p_session_connect_fail");
	async_event.set_steamid_all("user_id", event->m_steamIDRemote);
	async_event.set((char*)"error", static_cast<double>(event->m_eP2PSessionError));
	async_event.dispatch();
}

}

void steam_net_callbacks_t::p2p_session_request(P2PSessionRequest_t* event)
{
	CSteamID remote_id = event->m_steamIDRemote;
	steam_net_dispatch_p2p_event("p2p_session_request", remote_id);

	ISteamNetworking* networking = steam_net_client_networking();
	ISteamMatchmaking* matchmaking = SteamMatchmaking();
	if (!steam_net_client_auto_accept_p2p_sessions || !networking || !matchmaking) {
		return;
	}

	int member_count = matchmaking->GetNumLobbyMembers(steam_lobby_current);
	for (int index = 0; index < member_count; index++) {
		if (matchmaking->GetLobbyMemberByIndex(steam_lobby_current, index) == remote_id) {
			networking->AcceptP2PSessionWithUser(remote_id);
			break;
		}
	}
}

void steam_net_callbacks_t::p2p_session_connect_fail(P2PSessionConnectFail_t* event)
{
	steam_net_event async_event((char*)"p2p_session_connect_fail");
	async_event.set_steamid_all("user_id", event->m_steamIDRemote);
	async_event.set((char*)"error", static_cast<double>(event->m_eP2PSessionError));
	async_event.dispatch();
}

#pragma region Client API

YYEXPORT void steam_net_client_set_auto_accept_p2p_sessions(RValue& result, CInstance*, CInstance*, int, RValue* args)
{
	steam_net_set_auto_accept_p2p_sessions_impl(result, args, steam_net_client_auto_accept_p2p_sessions);
}

YYEXPORT void steam_net_client_accept_p2p_session(RValue& result, CInstance*, CInstance*, int, RValue* args)
{
	steam_net_accept_p2p_session_impl(result, args, steam_net_client_networking());
}

YYEXPORT void steam_net_client_close_p2p_session(RValue& result, CInstance*, CInstance*, int, RValue* args)
{
	steam_net_close_p2p_session_impl(result, args, steam_net_client_networking());
}

YYEXPORT void steam_net_client_packet_set_type(RValue& result, CInstance*, CInstance*, int, RValue* args)
{
	steam_net_packet_set_type_impl(result, args, steam_net_client_packet_state);
}

YYEXPORT void steam_net_client_packet_set_send_options(RValue& result, CInstance*, CInstance*, int, RValue* args)
{
	steam_net_packet_set_send_options_impl(result, args, steam_net_client_packet_state);
}

YYEXPORT void steam_net_client_packet_send(RValue& result, CInstance*, CInstance*, int argc, RValue* args)
{
	steam_net_packet_send_impl(result, argc, args, steam_net_client_networking(), steam_net_client_packet_state, steam_net_client_send_diagnostic, "client");
}

YYEXPORT void steam_net_client_packet_get_last_send_diagnostic(RValue& result, CInstance*, CInstance*, int, RValue*)
{
	steam_net_packet_get_last_send_diagnostic_impl(result, steam_net_client_send_diagnostic);
}

YYEXPORT void steam_net_client_packet_receive(RValue& result, CInstance*, CInstance*, int argc, RValue* args)
{
	steam_net_packet_receive_impl(result, argc, args, steam_net_client_networking(), steam_net_client_packet_state);
}

YYEXPORT void steam_net_client_packet_get_size(RValue& result, CInstance*, CInstance*, int, RValue*)
{
	steam_net_packet_get_size_impl(result, steam_net_client_packet_state);
}

YYEXPORT void steam_net_client_packet_get_data(RValue& result, CInstance*, CInstance*, int, RValue* args)
{
	steam_net_packet_get_data_impl(result, args, steam_net_client_packet_state);
}

YYEXPORT void steam_net_client_packet_get_sender_id(RValue& result, CInstance*, CInstance*, int, RValue*)
{
	steam_net_packet_get_sender_id_impl(result, steam_net_client_packet_state);
}

#pragma endregion

#pragma region Game Server API

YYEXPORT void steam_net_game_server_set_auto_accept_p2p_sessions(RValue& result, CInstance*, CInstance*, int, RValue* args)
{
	steam_net_set_auto_accept_p2p_sessions_impl(result, args, steam_net_game_server_auto_accept_p2p_sessions);
}

YYEXPORT void steam_net_game_server_accept_p2p_session(RValue& result, CInstance*, CInstance*, int, RValue* args)
{
	steam_net_accept_p2p_session_impl(result, args, steam_net_game_server_networking());
}

YYEXPORT void steam_net_game_server_close_p2p_session(RValue& result, CInstance*, CInstance*, int, RValue* args)
{
	steam_net_close_p2p_session_impl(result, args, steam_net_game_server_networking());
}

YYEXPORT void steam_net_game_server_packet_set_type(RValue& result, CInstance*, CInstance*, int, RValue* args)
{
	steam_net_packet_set_type_impl(result, args, steam_net_game_server_packet_state);
}

YYEXPORT void steam_net_game_server_packet_set_send_options(RValue& result, CInstance*, CInstance*, int, RValue* args)
{
	steam_net_packet_set_send_options_impl(result, args, steam_net_game_server_packet_state);
}

YYEXPORT void steam_net_game_server_packet_send(RValue& result, CInstance*, CInstance*, int argc, RValue* args)
{
	steam_net_packet_send_impl(result, argc, args, steam_net_game_server_networking(), steam_net_game_server_packet_state, steam_net_game_server_send_diagnostic, "game_server");
}

YYEXPORT void steam_net_game_server_packet_get_last_send_diagnostic(RValue& result, CInstance*, CInstance*, int, RValue*)
{
	steam_net_packet_get_last_send_diagnostic_impl(result, steam_net_game_server_send_diagnostic);
}

YYEXPORT void steam_net_game_server_packet_receive(RValue& result, CInstance*, CInstance*, int argc, RValue* args)
{
	steam_net_packet_receive_impl(result, argc, args, steam_net_game_server_networking(), steam_net_game_server_packet_state);
}

YYEXPORT void steam_net_game_server_packet_get_size(RValue& result, CInstance*, CInstance*, int, RValue*)
{
	steam_net_packet_get_size_impl(result, steam_net_game_server_packet_state);
}

YYEXPORT void steam_net_game_server_packet_get_data(RValue& result, CInstance*, CInstance*, int, RValue* args)
{
	steam_net_packet_get_data_impl(result, args, steam_net_game_server_packet_state);
}

YYEXPORT void steam_net_game_server_packet_get_sender_id(RValue& result, CInstance*, CInstance*, int, RValue*)
{
	steam_net_packet_get_sender_id_impl(result, steam_net_game_server_packet_state);
}

#pragma endregion

#pragma region Backwards-compatible Client API aliases

YYEXPORT void steam_net_set_auto_accept_p2p_sessions(RValue& result, CInstance* self, CInstance* other, int argc, RValue* args)
{
	steam_net_client_set_auto_accept_p2p_sessions(result, self, other, argc, args);
}

YYEXPORT void steam_net_accept_p2p_session(RValue& result, CInstance* self, CInstance* other, int argc, RValue* args)
{
	steam_net_client_accept_p2p_session(result, self, other, argc, args);
}

YYEXPORT void steam_net_close_p2p_session(RValue& result, CInstance* self, CInstance* other, int argc, RValue* args)
{
	steam_net_client_close_p2p_session(result, self, other, argc, args);
}

YYEXPORT void steam_net_packet_set_type(RValue& result, CInstance* self, CInstance* other, int argc, RValue* args)
{
	steam_net_client_packet_set_type(result, self, other, argc, args);
}

YYEXPORT void steam_net_packet_send(RValue& result, CInstance* self, CInstance* other, int argc, RValue* args)
{
	if (argc > 3) {
		steam_net_packet_set_type_impl(result, args + 3, steam_net_client_packet_state);
	}
	if (argc > 4) {
		steam_net_client_packet_state.send_channel = YYGetInt32(args, 4);
	}
	steam_net_client_packet_send(result, self, other, argc, args);
}

YYEXPORT void steam_net_packet_receive(RValue& result, CInstance* self, CInstance* other, int argc, RValue* args)
{
	steam_net_client_packet_receive(result, self, other, argc, args);
}

YYEXPORT void steam_net_packet_get_size(RValue& result, CInstance* self, CInstance* other, int argc, RValue* args)
{
	steam_net_client_packet_get_size(result, self, other, argc, args);
}

YYEXPORT void steam_net_packet_get_data(RValue& result, CInstance* self, CInstance* other, int argc, RValue* args)
{
	steam_net_client_packet_get_data(result, self, other, argc, args);
}

YYEXPORT void steam_net_packet_get_sender_id(RValue& result, CInstance* self, CInstance* other, int argc, RValue* args)
{
	steam_net_client_packet_get_sender_id(result, self, other, argc, args);
}

#pragma endregion
