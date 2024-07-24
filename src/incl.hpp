#pragma once
#define WIN32_LEAN_AND_MEAN
//#define SERVER_IMPL

#include <windows.h>
#include <Xinput.h>
#include <ViGEm/Client.h>

#include <ws2tcpip.h>
#include <winsock2.h>

#include <iostream>
#include <thread>
#include <condition_variable>
#include <type_traits>

#include "types.hpp"

#ifndef NDEBUG
#	define DEBUG_BUILD
#endif

#define ASSERT(x) if(!(x)){*(int*)0 = 0;}

constexpr u32 network_chunk_size = 4096;
constexpr u32 screen_send_interval_ms = 1000 / 60;

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
	MESSAGE_INFO_CHANGED_CAPTURED_SCREEN_DIMENSIONS,

	MESSAGE_ERROR_NONE,
	MESSAGE_ERROR_ROOM_AT_FULL_CAPACITY,
	MESSAGE_ERROR_INDEX_OUT_OF_BOUNDS,
	MESSAGE_ERROR_HOST_COULD_NOT_ALLOCATE_PAD,
	MESSAGE_ERROR_CLIENT_NOT_CONNECTED,
	MESSAGE_ERROR_ROOM_NO_LONGER_EXISTS
};

enum ControllerType
{
	CONTROLLER_TYPE_XBOX = 0,
	CONTROLLER_TYPE_DUALSHOCK,
};

enum ThreadType
{
	THREAD_TYPE_CLIENT,
	THREAD_TYPE_STOP,
};

struct PartialCapture
{
	u32 begin_index;
	u32 end_index;
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


static inline void SendMsg(SOCKET sock, Message msg)
{
	s32 error_msg = send(sock, reinterpret_cast<char*>(&msg), sizeof(Message), 0);
	if (error_msg == SOCKET_ERROR) {
		std::cout << "Error with Send func: " << WSAGetLastError() << "\n";
	}
}

static inline Message ReceiveMsg(SOCKET sock) 
{
	Message msg;
	s32 error_msg = recv(sock, reinterpret_cast<char*>(&msg), sizeof(Message), 0);
	if (error_msg == SOCKET_ERROR) {
		std::cout << "Error with Receive func: " << WSAGetLastError() << "\n";
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
		std::cout << "Error with Send func: " << WSAGetLastError() << "\n";
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
		std::cout << "Error with Receive func: " << WSAGetLastError() << "\n";
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
			std::cout << "Error with ReceiveBuffer func: " << WSAGetLastError() << "\n";
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
			std::cout << "Error with ReceiveBuffer func: " << WSAGetLastError() << "\n";
			return false;
		}
		total_bytes_received += bytes_received;
	}

	return true;
}

