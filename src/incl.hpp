#pragma once
#define WIN32_LEAN_AND_MEAN
//#define IMPLEMENTATION_SERVER
//#define IMPLEMENTATION_CONSOLE // Unsafe to use, for debug purposes
#define IMPLEMENTATION_WX
//Provide compatibility with wxWidgets
#ifndef _CRT_SECURE_NO_WARNINGS
#	define _CRT_SECURE_NO_WARNINGS
#endif

#include <windows.h>
#include <Xinput.h>
#include <Audioclient.h>
#include <Mmdeviceapi.h>

#include <ViGEm/Client.h>

#include <ws2tcpip.h>
#include <winsock2.h>

#include <iostream>
#include <format>
#include <thread>
#include <condition_variable>
#include <type_traits>

#include "types.hpp"

#ifndef NDEBUG
#	define DEBUG_BUILD
#endif

#undef max
#undef min

#define XE_ASSERT(x, msg, ...)												\
if(!(x))																	\
{																			\
	Log::Format("[Assertion Failed!] [{}, {}]: ", __FUNCTION__, __LINE__);	\
	Log::Format(msg, __VA_ARGS__);											\
	*(int*)0 = 0;															\
}

#define SPAWN_THREAD(body) std::thread([&](){body;})
#define XE_KEY_PRESS(key, shl) ((GetAsyncKeyState(key) & 0x8000) ? 1 : 0) << shl

static constexpr u32 network_chunk_size = 4096;
static constexpr u32 screen_send_interval_ms = 1000 / 60;
static constexpr s32 send_buffer_width = 500;
static constexpr s32 send_buffer_height = 500;
static constexpr u32 max_window_name_length = 128;
static constexpr u32 max_window_enumerations = 128;
static constexpr u32 audio_packets_per_single_send = 10;
static constexpr u32 audio_packets_per_fast_send = 7;

namespace WX {
	extern const u32 components_struct_size;
	extern const u32 connection_frame_class_size;
	extern const u32 main_frame_class_size;
	extern const u32 room_creation_frame_class_size;
}

enum Message
{
	MESSAGE_EMPTY = 0,

	MESSAGE_REQUEST_ROOM_CREATE,
	MESSAGE_REQUEST_ROOM_JOIN,
	MESSAGE_REQUEST_ROOM_QUIT,
	MESSAGE_REQUEST_ROOM_QUERY,
	MESSAGE_REQUEST_SEND_PAD_DATA,
	MESSAGE_REQUEST_SEND_COMPLETE_VIDEO_CAPTURE,
	MESSAGE_REQUEST_SEND_PARTIAL_VIDEO_CAPTURE,
	MESSAGE_REQUEST_SEND_AUDIO_CAPTURE,
	MESSAGE_REQUEST_SEND_RESAMPLED_AUDIO,

	MESSAGE_INFO_SERVER_PING,
	MESSAGE_INFO_ROOM_JOINED,
	MESSAGE_INFO_ROOM_CLOSING,
	MESSAGE_INFO_CLIENT_JOINING_ROOM,
	MESSAGE_INFO_PAD_ALLOCATED,
	MESSAGE_INFO_CLIENT_DISCONNECTED,

	MESSAGE_ERROR_NONE,
	MESSAGE_ERROR_ROOM_AT_FULL_CAPACITY,
	MESSAGE_ERROR_INDEX_OUT_OF_BOUNDS,
	MESSAGE_ERROR_HOST_COULD_NOT_ALLOCATE_PAD,
	MESSAGE_ERROR_CLIENT_NOT_CONNECTED,
	MESSAGE_ERROR_ROOM_NO_LONGER_EXISTS
};

enum FixedBufferType : u8
{
	FIXED_BUFFER_TYPE_SERVER = 0,
	FIXED_BUFFER_TYPE_HOST,
	FIXED_BUFFER_TYPE_CLIENT,
	FIXED_BUFFER_TYPE_WX
};

//Tracks all the direct allocations
enum ServerAllocations : u8
{
	SERVER_ALLOCATIONS_SIZE
};

enum HostAllocations : u8
{
	HOST_ALLOCATIONS_COMPRESSED_BUF = 0,
	HOST_ALLOCATIONS_PREV_COMPRESSED_BUF,
	HOST_ALLOCATIONS_UNCOMPRESSED_BUF,
	HOST_ALLOCATIONS_WINDOW_ENUM,
	HOST_ALLOCATIONS_CLIENT_CONNECTIONS,
	HOST_ALLOCATIONS_SIZE
};

enum ClientAllocations : u8
{
	CLIENT_ALLOCATIONS_COMPRESSED_SCREEN_BUFFER = 0,
	CLIENT_ALLOCATIONS_SIZE,
};

enum WxAllocations : u8
{
	WX_ALLOCATIONS_COMPONENTS = 0,
	WX_ALLOCATIONS_CONNECTION_FRAME,
	WX_ALLOCATIONS_MAIN_FRAME,
	WX_ALLOCATIONS_ROOM_CREATION_FRAME,
	WX_ALLOCATIONS_SIZE
};

struct ConnectionInfo
{
	PVIGEM_TARGET pad_handle;
	bool connected = false;
};

struct WindowEnumeration
{
	char window_names[max_window_enumerations * max_window_name_length];
	u32 windows_count;
};

const u32 g_client_allocations_offsets[CLIENT_ALLOCATIONS_SIZE] = {
	(send_buffer_width * send_buffer_height * 4) / 10
};

const u32 g_host_allocations_offsets[HOST_ALLOCATIONS_SIZE] = {
	(send_buffer_width * send_buffer_height * 4) / 10,
	(send_buffer_width * send_buffer_height * 4) / 10,
	send_buffer_width* send_buffer_height * 4,
	sizeof(WindowEnumeration),
	sizeof(ConnectionInfo) * XUSER_MAX_COUNT
};

const u32 g_wx_allocations_offsets[WX_ALLOCATIONS_SIZE] = {
	WX::components_struct_size,
	WX::connection_frame_class_size,
	WX::main_frame_class_size,
	WX::room_creation_frame_class_size
};

enum ControllerType
{
	CONTROLLER_TYPE_KEYBOARD = 0,
	CONTROLLER_TYPE_XBOX,
	CONTROLLER_TYPE_DUALSHOCK,
};

enum ThreadType
{
	THREAD_TYPE_CLIENT,
	THREAD_TYPE_STOP,
};

struct PadSignal
{
	u32 pad_number;
	XINPUT_GAMEPAD pad_state;
};

struct Room
{
	//Edited by the server
	u64 id;
	SOCKET host_socket = 0;

	struct
	{
		SOCKET sock;
		bool connected;
		u32 resampled_audio_frames_left;
	} connected_sockets[4] = {};

	//Edited by the host
	struct Info
	{
		char name[16];
		u16 max_pads;
		u16 current_pads;
		//Unused
		u32 host_window_width;
		u32 host_window_height;
	} info;

	struct SyncPrimitives
	{
		std::mutex mtx;
		std::condition_variable notify_cv;
	};
	u32 sync_primitives_index;
	Message connecting_message;
};

struct ControllerData
{
	u32 dualshock_count, xbox_count;
	s32 dualshock_handles[4];
	u8 xbox_handles[4];
};

namespace Log
{
	template <class... _Types>
	using FormatString = std::_Basic_format_string<char, std::type_identity_t<_Types>...>;

	//Formats and prints to the console
	template <class... _Types>
	static inline void Format(FormatString<_Types...> _Fmt, _Types&&... _Args)
	{
		std::cout << std::format(_Fmt, static_cast<_Types&&>(_Args)...);
	}

	namespace Debug
	{
		template <class... _Types>
		static inline void Format(FormatString<_Types...> _Fmt, _Types&&... _Args)
		{
#ifdef DEBUG_BUILD
			std::cout << std::format(_Fmt, static_cast<_Types&&>(_Args)...);
#endif
		}
	}
}

static inline void SendMsg(SOCKET sock, Message msg)
{
	s32 error_msg = send(sock, reinterpret_cast<char*>(&msg), sizeof(Message), 0);
	if (error_msg == SOCKET_ERROR) {
		Log::Format("Error with Send func: {}\n", WSAGetLastError());
	}
}

static inline Message ReceiveMsg(SOCKET sock)
{
	Message msg;
	s32 error_msg = recv(sock, reinterpret_cast<char*>(&msg), sizeof(Message), 0);
	if (error_msg == SOCKET_ERROR) {
		Log::Format("Error with Receive func: {}\n", WSAGetLastError());
		return MESSAGE_EMPTY;
	}

	return msg;
}

template<typename T>
static inline bool Send(SOCKET sock, T& data)
{
	static_assert(std::is_trivially_copyable_v<T>, "The type needs to be trivially copiable");
	s32 error_msg = send(sock, reinterpret_cast<char*>(&data), sizeof(T), 0);
	if (error_msg == SOCKET_ERROR) {
		Log::Format("Error with Send func: {}\n", WSAGetLastError());
		return false;
	}

	return true;
}

template<typename T>
static inline bool Receive(SOCKET sock, T* data)
{
	static_assert(std::is_trivially_copyable_v<T>, "The type needs to be trivially copiable");
	s32 error_msg = recv(sock, reinterpret_cast<char*>(data), sizeof(T), 0);
	if (error_msg == SOCKET_ERROR) {
		Log::Format("Error with Receive func: {}\n", WSAGetLastError());
		return false;
	}

	return true;
}

static inline bool SendBuffer(SOCKET sock, void* data, u32 size)
{
	u32 total_bytes_sent = 0;
	while (total_bytes_sent < size) {
		char* current_ptr = static_cast<char*>(data) + total_bytes_sent;
		s32 len = std::min(network_chunk_size, size - total_bytes_sent);

		u32 bytes_sent = send(sock, current_ptr, len, 0);
		if (bytes_sent == SOCKET_ERROR) {
			Log::Format("Error with SendBuffer func: {}\n", WSAGetLastError());
			return false;
		}
		total_bytes_sent += bytes_sent;
	}

	return true;
}

static inline bool ReceiveBuffer(SOCKET sock, void* data, u32 size)
{
	u32 total_bytes_received = 0;
	while (total_bytes_received < size) {
		char* current_ptr = static_cast<char*>(data) + total_bytes_received;
		s32 len = std::min(network_chunk_size, size - total_bytes_received);

		u32 bytes_received = recv(sock, current_ptr, len, 0);
		if (bytes_received == SOCKET_ERROR) {
			Log::Format("Error with ReceiveBuffer func: {}\n", WSAGetLastError());
			return false;
		}
		total_bytes_received += bytes_received;
	}

	return true;
}


