#pragma once
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <Xinput.h>
#include <ViGEm/Client.h>

#include <ws2tcpip.h>
#include <winsock2.h>

#include <iostream>
#include <thread>
#include <condition_variable>
#include <type_traits>

//#include <SDL.h>

#include "types.hpp"

enum Message
{
	MESSAGE_EMPTY = 0,

	MESSAGE_REQUEST_ROOM_CREATE,
	MESSAGE_REQUEST_ROOM_JOIN,
	MESSAGE_REQUEST_ROOM_QUERY,
	MESSAGE_REQUEST_SEND_PAD_DATA,

	MESSAGE_INFO_ROOM_JOINED,
	MESSAGE_INFO_CLIENT_JOINING_ROOM,
	MESSAGE_INFO_PAD_ALLOCATED,

	MESSAGE_ERROR_NONE,
	MESSAGE_ERROR_ROOM_AT_FULL_CAPACITY,
	MESSAGE_ERROR_HOST_COULD_NOT_ALLOCATE_PAD,
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

struct PadSignal
{
	u32 pad_number;
	XINPUT_GAMEPAD pad_state;
};



struct Room
{
	//Edited by the server
	SOCKET host_socket = 0;
	struct {
		SOCKET sock;
		bool slot_taken;
	} connected_sockets[4] = {};
	//Edited by the host
	struct Info {
		char name[16];
		u16 max_pads;
		u16 current_pads;
	} info;

	std::mutex* mtx;
	std::condition_variable* notify_cv;
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



