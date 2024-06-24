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

#include "types.hpp"

enum Message
{
	MESSAGE_REQUEST_ROOM_CREATE = 0,
	MESSAGE_REQUEST_ROOM_JOIN,
	MESSAGE_REQUEST_ROOM_QUERY,
	MESSAGE_REQUEST_SEND_PAD_DATA,

	MESSAGE_INFO_ROOM_JOINED,

	MESSAGE_ERROR_ROOM_AT_FULL_CAPACITY,
};

enum ThreadType
{
	THREAD_TYPE_CLIENT,
	THREAD_TYPE_STOP,
};

//TODO this needs to be moved elsewhere some time in the future
static void close_host_socket(SOCKET socket)
{
	closesocket(socket);
	WSACleanup();
}

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
	char name[16];
	u16 max_pads;
	u16 current_pads;
};


