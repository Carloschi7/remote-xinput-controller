#pragma once
#define WIN32_LEAN_AND_MEAN
//#define SERVER_IMPL

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

static constexpr u32 network_chunk_size = 4096;
static constexpr u32 screen_send_interval_ms = 1000 / 60;
static constexpr s32 send_buffer_width = 500;
static constexpr s32 send_buffer_height = 500;
static constexpr u32 max_window_name_length = 128;
static constexpr u32 max_window_enumerations = 128;

enum Message
{
	MESSAGE_EMPTY = 0,

	MESSAGE_REQUEST_ROOM_CREATE,
	MESSAGE_REQUEST_ROOM_JOIN,
	MESSAGE_REQUEST_ROOM_QUIT,
	MESSAGE_REQUEST_ROOM_QUERY,
	MESSAGE_REQUEST_SEND_PAD_DATA,
	MESSAGE_REQUEST_SEND_COMPLETE_CAPTURE,
	MESSAGE_REQUEST_SEND_PARTIAL_CAPTURE,

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
	FIXED_BUFFER_TYPE_SERVER,
	FIXED_BUFFER_TYPE_HOST,
	FIXED_BUFFER_TYPE_CLIENT
};

//Tracks all the direct allocations
enum ClientAllocations : u8
{
	CLIENT_ALLOCATIONS_DUALSHOCK_QUERY = 0,
	CLIENT_ALLOCATIONS_COMPRESSED_SCREEN_BUFFER,
	CLIENT_ALLOCATIONS_SIZE,
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

enum ServerAllocations : u8
{
	SERVER_ALLOCATIONS_SIZE
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
	XUSER_MAX_COUNT * sizeof(s32),
	(send_buffer_width * send_buffer_height * 4) / 10 
};

const u32 g_host_allocations_offsets[HOST_ALLOCATIONS_SIZE] = {
	(send_buffer_width * send_buffer_height * 4) / 10,
	(send_buffer_width * send_buffer_height * 4) / 10,
	send_buffer_width* send_buffer_height * 4,
	sizeof(WindowEnumeration),
	sizeof(ConnectionInfo) * XUSER_MAX_COUNT
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
	} connected_sockets[4] = {};

	//Edited by the host
	struct Info
	{
		char name[16];
		u16 max_pads;
		u16 current_pads;
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
		s32 len = min(network_chunk_size, size - total_bytes_sent);

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
		s32 len = min(network_chunk_size, size - total_bytes_received);

		u32 bytes_received = recv(sock, current_ptr, len, 0);
		if (bytes_received == SOCKET_ERROR) {
			Log::Format("Error with ReceiveBuffer func: {}\n", WSAGetLastError());
			return false;
		}
		total_bytes_received += bytes_received;
	}

	return true;
}

#define XE_ASSERT(x, msg, ...)												\
if(!(x))																	\
{																			\
	Log::Format("[Assertion Failed!] [{}, {}]: ", __FUNCTION__, __LINE__);	\
	Log::Format(msg, __VA_ARGS__);											\
	*(int*)0 = 0;															\
}

#define XE_KEY_PRESS(key, shl) ((GetAsyncKeyState(key) & 0x8000) ? 1 : 0) << shl
