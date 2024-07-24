#include "client.hpp"
#include "JoyShockLibrary.h"

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

	GameWindowData window_data;
	Receive(client_socket, &room_id);
	Receive(client_socket, &window_data.src_width);
	Receive(client_socket, &window_data.src_height);
	//At the beginning, src and dest dimensions are the same
	window_data.dst_width = window_data.src_width;
	window_data.dst_height = window_data.src_height;

	std::cout << "Connection was successful, {X to quit the room}!\n";

	HWND game_window = InitGameWindowContext(&window_data);
	SetTimer(game_window, 1, screen_send_interval_ms, nullptr);

	std::atomic<char> quit_signal = 0;
	std::atomic<bool> terminate_signal = false;
	std::thread quit_thread, recv_thread;

	quit_thread = std::thread([&quit_signal]() {
		while (true) {
			char ch; std::cin >> ch;
			if (ch == 'X') { quit_signal = ch; break; }
		} });


	recv_thread = std::thread([&]() {
		u32 max_changed_regions = 10000;
		PartialCapture* captures = new PartialCapture[max_changed_regions];
		while (true) {
			Message msg = ReceiveMsg(client_socket);

			//std::scoped_lock lk{ window_data.buffer_mutex };
			switch (msg) {
			case MESSAGE_REQUEST_SEND_COMPLETE_CAPTURE: {
				ReceiveBuffer(client_socket, window_data.buffer.data(), window_data.buffer.size());
			}break;
			case MESSAGE_REQUEST_SEND_PARTIAL_CAPTURE: {
				u32 changed_regions;
				Receive(client_socket, &changed_regions);
				if (changed_regions >= max_changed_regions) {
					delete[] captures;
					max_changed_regions = changed_regions;
					captures = new PartialCapture[max_changed_regions];
				}

				ReceiveBuffer(client_socket, captures, changed_regions * sizeof(PartialCapture));

				for (u32 i = 0; i < changed_regions; i++) {
					PartialCapture& capture = captures[i];
					ReceiveBuffer(client_socket, window_data.buffer.data() + capture.begin_index, capture.end_index - capture.begin_index);
				}
			}break;
			case MESSAGE_INFO_CHANGED_CAPTURED_SCREEN_DIMENSIONS:
				Receive(client_socket, &window_data.src_width);
				Receive(client_socket, &window_data.src_height);
				window_data.buffer.resize(window_data.src_width * window_data.src_height * 4);
				break;
			case MESSAGE_REQUEST_ROOM_QUIT:
			case MESSAGE_ERROR_ROOM_NO_LONGER_EXISTS:
				terminate_signal = true;
				delete[] captures;
				return;
			}
		}
		delete[] captures;
	});

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
			pad_state.Gamepad.bLeftTrigger = static_cast<u8>(state.lTrigger * 255.0f);
			pad_state.Gamepad.bRightTrigger = static_cast<u8>(state.rTrigger * 255.0f);
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
			break;
		}

		if (std::memcmp(&prev_pad_state.Gamepad, &pad_state.Gamepad, sizeof(XINPUT_GAMEPAD)) != 0) {

			SendMsg(client_socket, MESSAGE_REQUEST_SEND_PAD_DATA);
			Send(client_socket, room_id);
			Send(client_socket, pad_state.Gamepad);

			//TODO handle MESSAGE_ERROR_CLIENT_NOT_CONNECTED
			prev_pad_state = pad_state;
		}
	
		if (terminate_signal) {
			std::cout << "Host closed the room or its no longer reachable, press anything to close\n";
			break;
		}

		MSG win_msg = {};
		PeekMessage(&win_msg, 0, 0, 0, PM_REMOVE);
		TranslateMessage(&win_msg);
		DispatchMessage(&win_msg);
	}

	recv_thread.join();
	quit_thread.join();
	JslDisconnectAndDisposeAll();
	DestroyGameWindowContext(game_window, window_data.wnd_class);
}

LRESULT CALLBACK GameWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) 
{
	static GameWindowData* window_data = nullptr;
	switch (uMsg) {
	case WM_DESTROY:
		PostQuitMessage(0);
		window_data = nullptr;
		return 0;
	case WM_PAINT:
		if (window_data) {
			//std::scoped_lock lk{ window_data->buffer_mutex };
			FetchCaptureToGameWindow(hwnd, window_data);
		}
		break;
	case WM_SIZE:
		//TODO there is a bug that happens at resizing freezing the screen, fix it
		if (window_data) {
			RECT rect;
			GetWindowRect(hwnd, &rect);
			window_data->dst_width = rect.right - rect.left;
			window_data->dst_height = rect.bottom - rect.top;
		}
		return 0;
	case WM_TIMER:
		//INFO: probably there is a better way to establish a 60fps stream, but at the moment this is fine
		InvalidateRect(hwnd, nullptr, FALSE);
		break;
	case WM_CREATE: {
		auto lp_create = reinterpret_cast<LPCREATESTRUCT>(lParam);
		window_data = reinterpret_cast<GameWindowData*>(lp_create->lpCreateParams);
	}	break;
	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

HWND InitGameWindowContext(GameWindowData* window_data)
{
	if (!window_data) 
		return nullptr;

	window_data->buffer.resize(window_data->src_width * window_data->src_height * 4);

	HINSTANCE instance = GetModuleHandle(nullptr);
	const wchar_t class_name[] = L"Game window";
	WNDCLASS window_class = {};

	window_class.lpfnWndProc = GameWindowProc;
	window_class.hInstance = instance;
	window_class.lpszClassName = class_name;
	window_class.cbWndExtra = sizeof(GameWindowData);

	RegisterClass(&window_class);
	window_data->wnd_class = window_class;

	HWND game_window = CreateWindowEx(0, class_name, L"Game Window",
		WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
		window_data->src_width, window_data->src_height, 
		nullptr, nullptr, instance, window_data);

	if (!game_window) {
		std::cout << "Failed to create game window\n";
		u32 error = GetLastError();
		return nullptr;
	}

	ShowWindow(game_window, SW_SHOW);
	return game_window;
}

void FetchCaptureToGameWindow(HWND& hwnd, GameWindowData* window_data)
{
	ASSERT(window_data);

	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(hwnd, &ps);

	// Create a bitmap in memory
	BITMAPINFO bmi = { 0 };
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = window_data->src_width;
	bmi.bmiHeader.biHeight = -window_data->src_height; // Use negative height for top-down DIB
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	// Create a memory device context compatible with the window DC
	HDC memDC = CreateCompatibleDC(hdc);
	HBITMAP hBitmap = CreateCompatibleBitmap(hdc, window_data->src_width, window_data->src_height);
	HGDIOBJ gdi_obj = SelectObject(memDC, hBitmap);

	if (SetDIBits(hdc, hBitmap, 0, window_data->src_height, window_data->buffer.data(), &bmi, DIB_RGB_COLORS) == 0) {
		std::cout << "SetDIBits failed\n";
	}

	//if (StretchDIBits(hdc, 0, 0, window_data->original_width, window_data->original_height,
	//	0, 0, window_data->width, window_data->height, window_data->buffer.data(), &bmi, DIB_RGB_COLORS, SRCCOPY) == 0) {
	//	std::cout << "SetDIBits failed\n";
	//}

	// Blit the bitmap to the window
	if (StretchBlt(hdc, 0, 0, window_data->dst_width, window_data->dst_height, memDC, 0, 0,
		window_data->src_width, window_data->src_height, SRCCOPY) == 0) {
		std::cout << "StretchBlt failed\n";
	}

	/*if (BitBlt(hdc, 0, 0, window_data->width, window_data->height, memDC, 0, 0, SRCCOPY) == 0) {
		std::cout << "BitBlt failed\n";
	}*/

	// Clean up
	DeleteDC(memDC);
	DeleteObject(hBitmap);

	EndPaint(hwnd, &ps);
}

void DestroyGameWindowContext(HWND& hwnd, WNDCLASS& wnd_class)
{
	DestroyWindow(hwnd);
	UnregisterClass(wnd_class.lpszClassName, wnd_class.hInstance);
}
