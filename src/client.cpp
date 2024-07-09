#include "client.hpp"
#include "JoyShockLibrary.h"
#include <vector>



void QueryRooms(SOCKET client_socket)
{
	SendMsg(client_socket, MESSAGE_REQUEST_ROOM_QUERY);

	u32 rooms_count;
	Receive(client_socket, &rooms_count);

	std::cout << "Available rooms          Room name          Users Connected          Max users\n";
	for (u32 i = 0; i < rooms_count; i++) {
		Room::Info room_info;
		Receive(client_socket, &room_info);

		std::string name_padding = "                   ";
		for (u32 i = 0; i < sizeof(room_info.name) && room_info.name[i] != 0; i++) {
			name_padding.pop_back();
		}

		std::cout << "Room: #" << i << ":                " << room_info.name << name_padding.c_str() <<
			room_info.current_pads << "                        " << room_info.max_pads << "\n";
	}
}

u32 QueryDualshockControllers(s32** controller_handles)
{
	s32 dualshock_controllers = JslConnectDevices();

	if (controller_handles && dualshock_controllers != 0) {
		*controller_handles = new s32[dualshock_controllers];
		JslGetConnectedDeviceHandles(*controller_handles, dualshock_controllers);
	}

	if (!controller_handles) {
		JslDisconnectAndDisposeAll();
	}

	return dualshock_controllers;
}

u32 QueryXboxControllers(bool slots[XUSER_MAX_COUNT])
{
	u32 xbox_controllers = 0;
	for (u32 i = 0; i < XUSER_MAX_COUNT; i++) {
		XINPUT_STATE unused = {};
		s32 pad_read_result = XInputGetState(i, &unused);
		if (pad_read_result == ERROR_SUCCESS) {
			xbox_controllers++;
			
			if(slots)
				slots[i] = true;
		}
	}

	return xbox_controllers;
}

void ClientImplementation(SOCKET client_socket)
{
	u32 chosen_room;
	u64 room_id;
	Message connection_status = MESSAGE_EMPTY;
	ControllerType controller_type;
	u32 controller_id;

	//Check for controllers controllers
	{
		s32* controller_handles = nullptr;
		u32 dualshock_controllers = QueryDualshockControllers(&controller_handles);
		bool xbox_slots[4] = {};
		u32 xbox_controllers = QueryXboxControllers(xbox_slots);

		if (dualshock_controllers == 0 && xbox_controllers == 0) {
			std::cout << "No joypad found, please connect a joypad to the system to be able to use remote play\n";
			JslDisconnectAndDisposeAll();
			return;
		}

		std::cout << "Select a controller to use for the room:\n";
		for (u32 i = 0; i < dualshock_controllers; i++) {
			std::cout << "#" << i << " -> PS4 pad: " << controller_handles[i] << "\n";
		}

		for (u32 i = 0, c = 0; i < XUSER_MAX_COUNT; i++) {
			if (xbox_slots[i]) {
				std::cout << "#" << dualshock_controllers + c << " -> xbox pad connected to slot " << i << "\n";
				c++;
			}
		}
		
		u32 sel;
		do {
			std::cin >> sel;
			controller_type = sel < dualshock_controllers ? CONTROLLER_TYPE_DUALSHOCK : CONTROLLER_TYPE_XBOX;
		} while (sel >= xbox_controllers + dualshock_controllers);

		if (controller_type == CONTROLLER_TYPE_XBOX) {
			//Simple list index to xbox pad index conversion
			sel -= dualshock_controllers;
			controller_id = 0;
			for (u32 i = 0; i <= sel; i++) {
				
				if (i != 0)
					controller_id++;

				for (; controller_id < XUSER_MAX_COUNT && !xbox_slots[controller_id]; controller_id++) {}
			}
		}
		else {
			controller_id = controller_handles[sel];
		}

		if (controller_handles)
			delete[] controller_handles;
	}

	do {
		std::cout << "Choose the room to connect to:" << std::endl;
		std::cin >> chosen_room;


		SendMsg(client_socket, MESSAGE_REQUEST_ROOM_JOIN);
		Send(client_socket, chosen_room);
		connection_status = ReceiveMsg(client_socket);

		if (connection_status == MESSAGE_ERROR_INDEX_OUT_OF_BOUNDS) {
			std::cout << "Please, insert a valid room ID\n";
		}
		else if (connection_status == MESSAGE_ERROR_ROOM_AT_FULL_CAPACITY) {
			std::cout << "Could not connect, the room is currently at full capacity\n";
		}
		else if (connection_status == MESSAGE_ERROR_HOST_COULD_NOT_ALLOCATE_PAD) {
			std::cout << "The host had issues creating a virtual pad, please try later\n";
		}

	} while (connection_status != MESSAGE_ERROR_NONE);
	Receive(client_socket, &room_id);

	std::cout << "Connection was successful, {X to quit the room}!\n";
	InitOpenGLContext();

	std::atomic<char> quit_signal;
	std::thread quit_thread([&quit_signal]() {
		while (true) {
			char ch; std::cin >> ch;
			if (ch == 'X') { quit_signal = ch; break; }
		} });

	const bool enable_screen_share = true;

	XINPUT_STATE prev_pad_state = {};
	while (true) {

		XINPUT_STATE pad_state = {};
		switch (controller_type) {
		case CONTROLLER_TYPE_DUALSHOCK: {
			//Conversion from DS4 data to XBOX data, so that on the host side
			//we can emulate every controller like it was an xbox one
			JOY_SHOCK_STATE state = JslGetSimpleState(controller_id);
			//We just care about buttons that range from dpad to the shape buttons
			pad_state.Gamepad.wButtons = static_cast<u16>(state.buttons & 0x0000FFFF);
			pad_state.Gamepad.bLeftTrigger = static_cast<u8>(state.lTrigger * 256.0f);
			pad_state.Gamepad.bRightTrigger = static_cast<u8>(state.rTrigger * 256.0f);
			pad_state.Gamepad.sThumbLX = static_cast<s16>(state.stickLX * (f32)INT16_MAX);
			pad_state.Gamepad.sThumbLY = static_cast<s16>(state.stickLY * (f32)INT16_MAX);
			pad_state.Gamepad.sThumbRX = static_cast<s16>(state.stickRX * (f32)INT16_MAX);
			pad_state.Gamepad.sThumbRY = static_cast<s16>(state.stickRY * (f32)INT16_MAX);
		}break;
		case CONTROLLER_TYPE_XBOX: {

			s32 pad_read_result = XInputGetState(controller_id, &pad_state);
			if (pad_read_result != ERROR_SUCCESS) {
			}
		}break;
		}

		//Find out if the user wants to quit the room
		if (quit_signal == 'X') {
			SendMsg(client_socket, MESSAGE_REQUEST_ROOM_QUIT);
			Send(client_socket, room_id);
			quit_thread.join();
			break;
		}

		if (std::memcmp(&prev_pad_state.Gamepad, &pad_state.Gamepad, sizeof(XINPUT_GAMEPAD)) != 0) {

			SendMsg(client_socket, MESSAGE_REQUEST_SEND_PAD_DATA);
			Send(client_socket, room_id);
			Send(client_socket, pad_state.Gamepad);

			Message msg = ReceiveMsg(client_socket);
			//TODO handle MESSAGE_ERROR_CLIENT_NOT_CONNECTED
			if (msg == MESSAGE_ERROR_ROOM_NO_LONGER_EXISTS) {
				std::cout << "Host closed the room or its no longer reachable, press anything to close\n";
				quit_thread.join();
				break;
			}

			prev_pad_state = pad_state;
		}
	
		//if (enable_screen_share) {
		//	Message msg = ReceiveMsg(client_socket);
		//	if (msg == MESSAGE_REQUEST_SEND_CAPTURED_SCREEN) {
		//		u32 buffer_size;
		//		Receive(client_socket, &buffer_size);
		//		std::vector<u8> buffer(buffer_size);
		//		ReceiveBuffer(client_socket, buffer.data(), buffer_size);
		//		FetchCaptureToOpenGL(buffer.data(), buffer_size);
		//	}
		//}
		//else {
		//}
		Sleep(30);
	}

	JslDisconnectAndDisposeAll();
	DestroyOpenGLContext();
}


void InitOpenGLContext()
{
	//TODO
}

void FetchCaptureToOpenGL(u8* buffer, u32 size)
{
	//TODO
}

void DestroyOpenGLContext()
{
	//TODO
}
