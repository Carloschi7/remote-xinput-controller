#include "client.hpp"
#include "JoyShockLibrary.h"
#include "mem.hpp"
#include "audio.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stbi_image.h>

void QueryRooms(SOCKET client_socket, Room::Info** rooms_data, u32* rooms_count)
{
	XE_ASSERT(rooms_count, "The pointer needs to be defined\n");
	SendMsg(client_socket, MESSAGE_REQUEST_ROOM_QUERY);

	Receive(client_socket, rooms_count);
	if (*rooms_count == 0)
		return;

	//TODO allocate this on a wx preallocated buffer?
	*rooms_data = new Room::Info[*rooms_count];

	for (u32 i = 0; i < *rooms_count; i++) {
		Receive(client_socket, (*rooms_data) + i);
	}
}

void PrintQueriedRooms(SOCKET client_socket)
{
	SendMsg(client_socket, MESSAGE_REQUEST_ROOM_QUERY);

	u32 rooms_count;
	Receive(client_socket, &rooms_count);

	Log::Format("Available rooms          Room name          Users Connected          Max users\n");
	for (u32 i = 0; i < rooms_count; i++) {
		Room::Info room_info;
		Receive(client_socket, &room_info);

		std::string name_padding = "                   ";
		for (u32 i = 0; i < sizeof(room_info.name) && room_info.name[i] != 0; i++) {
			name_padding.pop_back();
		}

		Log::Format("Room: #{}:                {}{}{}                        {}\n", i, room_info.name, name_padding.c_str(), room_info.current_pads, room_info.max_pads);
	}
}

u32 QueryDualshockCount()
{
	s32 dualshock_controllers = JslConnectDevices();
	if (dualshock_controllers > XUSER_MAX_COUNT) {
		//Max 4 controllers supported
		dualshock_controllers = XUSER_MAX_COUNT;
	}

	return dualshock_controllers;
}

u32 QueryXboxCount()
{
	u32 xbox_controllers = 0;
	for (u32 i = 0; i < XUSER_MAX_COUNT; i++) {
		XINPUT_STATE unused = {};
		s32 pad_read_result = XInputGetState(i, &unused);
		if (pad_read_result == ERROR_SUCCESS)
			xbox_controllers++;
	}

	return xbox_controllers;
}

u32 QueryDualshockControllers(s32* controller_handles, bool disconnect_jsl)
{
	s32 dualshock_controllers = JslConnectDevices();
	if (dualshock_controllers > XUSER_MAX_COUNT) {
		//Max 4 controllers supported
		dualshock_controllers = XUSER_MAX_COUNT;
	}

	if (controller_handles && dualshock_controllers != 0) {
		JslGetConnectedDeviceHandles(controller_handles, dualshock_controllers);
	}

	if (disconnect_jsl) {
		JslDisconnectAndDisposeAll();
	}

	return dualshock_controllers;
}

u32 QueryXboxControllers(u8* controller_handles)
{
	u32 xbox_controllers = 0;
	for (u32 i = 0; i < XUSER_MAX_COUNT; i++) {
		XINPUT_STATE unused = {};
		s32 pad_read_result = XInputGetState(i, &unused);
		if (pad_read_result == ERROR_SUCCESS) {
			xbox_controllers++;
			
			if (controller_handles)
				controller_handles[i] = 0x01;
		}
	}

	return xbox_controllers;
}

ControllerData QueryAllControllers()
{
	ControllerData data = {};
	data.xbox_count = QueryXboxControllers(data.xbox_handles);
	data.dualshock_count = QueryDualshockControllers(data.dualshock_handles, false);
	return data;
}

void ConsoleClientEntry(SOCKET client_socket)
{
	u32 chosen_room;
	Message connection_status = MESSAGE_EMPTY;
	ControllerType controller_type;
	u32 controller_id;

	Core::FixedBuffer fixed_buffer(FIXED_BUFFER_TYPE_CLIENT);
	
	//Check for controllers controllers
	{
		ControllerData controller_data = QueryAllControllers();
		const u32& xbox_count = controller_data.xbox_count;
		const u32& dualshock_count = controller_data.dualshock_count;

		Log::Format("Select a controller to use for the room:\n");
		//Assume only one keyboard connected, the main one will be selected
		const u32 keyboard_count = 1;
		Log::Format("#0 -> Main keyboard\n");
		for (u32 i = 0; i < dualshock_count; i++) {
			Log::Format("#{} -> PS4 pad: {}\n", i + keyboard_count, controller_data.dualshock_handles[i]);
		}

		for (u32 i = 0, c = 0; i < XUSER_MAX_COUNT; i++) {
			if (controller_data.xbox_handles[i] & 0x01) {
				Log::Format("#{}: ->xbox pad connected to slot {}\n", keyboard_count +  dualshock_count + c, i);
				c++;
			}
		}
		
		u32 sel;
		do {
			std::cin >> sel;
			if (sel == 0)
				controller_type = CONTROLLER_TYPE_KEYBOARD;
			else if (sel >= keyboard_count && sel < dualshock_count + keyboard_count)
				controller_type = CONTROLLER_TYPE_DUALSHOCK;
			else
				controller_type = CONTROLLER_TYPE_XBOX;
		} while (sel >= keyboard_count + xbox_count + dualshock_count);

		controller_id = 0;
		if (controller_type == CONTROLLER_TYPE_XBOX) {
			//Simple list index to xbox pad index conversion
			sel -= dualshock_count + keyboard_count;
			for (u32 i = 0; i <= sel; i++) {
				
				if (i != 0)
					controller_id++;

				for (; controller_id < XUSER_MAX_COUNT && !controller_data.xbox_handles[controller_id]; controller_id++) {}
			}
		}
		else if(controller_type == CONTROLLER_TYPE_DUALSHOCK) {
			controller_id = controller_data.dualshock_handles[sel - keyboard_count];
		}
	}

	do {
		Log::Format("Choose the room to connect to:\n");
		std::cin >> chosen_room;


		SendMsg(client_socket, MESSAGE_REQUEST_ROOM_JOIN);
		Send(client_socket, chosen_room);
		connection_status = ReceiveMsg(client_socket);

		if (connection_status == MESSAGE_ERROR_INDEX_OUT_OF_BOUNDS) {
			Log::Format("Please, insert a valid room ID\n");
		}
		else if (connection_status == MESSAGE_ERROR_ROOM_AT_FULL_CAPACITY) {
			Log::Format("Could not connect, the room is currently at full capacity\n");
		}
		else if (connection_status == MESSAGE_ERROR_HOST_COULD_NOT_ALLOCATE_PAD) {
			Log::Format("The host had issues creating a virtual pad, please try later\n");
		}

	} while (connection_status != MESSAGE_ERROR_NONE);

	Log::Format("Connection was successful, (X to quit the room)!\n");

	ExecuteClient(fixed_buffer, client_socket, controller_type, controller_id, true, nullptr);
}

void ExecuteClient(Core::FixedBuffer& fixed_buffer, SOCKET client_socket, ControllerType controller_type, 
	u32 controller_id, bool console_impl, void* extra_wx_data)
{
	u64 room_id;
	GameWindowData window_data;
	Receive(client_socket, &room_id);

	//At the beginning, src and dest dimensions are the same
	window_data.dst_width = send_buffer_width;
	window_data.dst_height = send_buffer_height;

	HWND game_window = InitGameWindowContext(fixed_buffer, &window_data);
	SetTimer(game_window, 1, screen_send_interval_ms, nullptr);

	std::atomic<char> quit_signal = 0;
	std::atomic<bool> terminate_signal = false;
	std::thread quit_thread, recv_thread;

	std::mutex payloads_mutex;
	std::list<Audio::Payload> payloads;

	if (console_impl) {
		quit_thread = std::thread([&quit_signal]() {
			while (true) {
				char ch; std::cin >> ch;
				if (ch == 'X') { quit_signal = ch; break; }
			} });
	}

	recv_thread = std::thread([&]() {

		while (true) {
			Message msg = ReceiveMsg(client_socket);
			u32 max_compressed_buf_size = g_client_allocations_offsets[CLIENT_ALLOCATIONS_COMPRESSED_SCREEN_BUFFER];

			switch (msg) {
			case MESSAGE_REQUEST_SEND_COMPLETE_VIDEO_CAPTURE: {
				std::unique_lock lk{ window_data.buffer_mutex };
				Receive(client_socket, &window_data.compressed_buffer_size);
				XE_ASSERT(window_data.compressed_buffer_size <= max_compressed_buf_size, "Compressed buffer size is too large\n");

				ReceiveBuffer(client_socket, window_data.buffer, window_data.compressed_buffer_size);
				lk.unlock();
			}break;
			case MESSAGE_REQUEST_SEND_PARTIAL_VIDEO_CAPTURE: {
				u32 diff_point, new_compressed_buffer_size;
				Receive(client_socket, &diff_point);
				Receive(client_socket, &new_compressed_buffer_size);

				std::unique_lock lk{ window_data.buffer_mutex };

				if (window_data.compressed_buffer_size != new_compressed_buffer_size)
					window_data.compressed_buffer_size = new_compressed_buffer_size;

				XE_ASSERT(window_data.compressed_buffer_size <= max_compressed_buf_size, "Compressed buffer size is too large\n");
				ReceiveBuffer(client_socket, window_data.buffer + diff_point, new_compressed_buffer_size - diff_point);
				lk.unlock();
			}break;
			case MESSAGE_REQUEST_SEND_AUDIO_CAPTURE: {
				std::scoped_lock lk{ payloads_mutex };
				u32 buffer_length;
				Receive(client_socket, &buffer_length);
				XE_ASSERT(buffer_length == Audio::unit_packet_size_in_bytes * audio_packets_per_single_send, "Size needs to be fixed");

				u32 single_send_size = buffer_length / audio_packets_per_single_send;
				for (u32 i = 0; i < 10; i++) {
					Audio::Payload payload;
					ReceiveBuffer(client_socket, payload.data, single_send_size);
					payloads.push_front(payload);
				}
			}break;
			case MESSAGE_REQUEST_ROOM_QUIT:
			case MESSAGE_ERROR_ROOM_NO_LONGER_EXISTS:
				terminate_signal = true;
				return;
			}
		}
		});

	XINPUT_STATE prev_pad_state = {};


	Audio::Device device;
	Audio::InitDevice(&device, false);

	while (true) {

		XINPUT_STATE pad_state = {};
		switch (controller_type) {
		case CONTROLLER_TYPE_KEYBOARD: {
			//TODO now the bindings are predefined here, when the UI gets implemented
			//this need to become customizable
			u16& buttons = pad_state.Gamepad.wButtons;

			buttons = 0;
			//Mapping of the dpad
			buttons |= XE_KEY_PRESS(VK_UP, 0);
			buttons |= XE_KEY_PRESS(VK_DOWN, 1);
			buttons |= XE_KEY_PRESS(VK_LEFT, 2);
			buttons |= XE_KEY_PRESS(VK_RIGHT, 3);

			//Mapping of the start and select/back
			buttons |= XE_KEY_PRESS(VK_ESCAPE, 4);
			buttons |= XE_KEY_PRESS(VK_SPACE, 5);

			//Mapping of L3/R3
			buttons |= XE_KEY_PRESS('Z', 6);
			buttons |= XE_KEY_PRESS('C', 7);

			//Mapping of RL
			buttons |= XE_KEY_PRESS('Q', 8);
			buttons |= XE_KEY_PRESS('E', 9);

			//Mapping of ABXY
			buttons |= XE_KEY_PRESS('K', 12);
			buttons |= XE_KEY_PRESS('L', 13);
			buttons |= XE_KEY_PRESS('J', 14);
			buttons |= XE_KEY_PRESS('I', 15);

			//Mapping of shoulder buttons (being on a keyboard gradual stated are impossible)
			pad_state.Gamepad.bLeftTrigger = (GetAsyncKeyState('1') & 0x8000) ? 0xFF : 0x00;
			pad_state.Gamepad.bRightTrigger = (GetAsyncKeyState('3') & 0x8000) ? 0xFF : 0x00;

			auto assign_thumb_from_keyboard = [](char first, char last) -> s32 {
				bool first_pressed = GetAsyncKeyState(first) & 0x8000;
				bool last_pressed = GetAsyncKeyState(last) & 0x8000;

				if ((!first_pressed && !last_pressed) || (first_pressed && last_pressed))
					return 0;
				else if (first_pressed)
					return -INT16_MAX;
				else
					return INT16_MAX;
			};

			pad_state.Gamepad.sThumbLX = assign_thumb_from_keyboard('A', 'D');
			pad_state.Gamepad.sThumbLY = assign_thumb_from_keyboard('S', 'W');
			pad_state.Gamepad.sThumbRX = assign_thumb_from_keyboard('F', 'H');
			pad_state.Gamepad.sThumbRY = assign_thumb_from_keyboard('G', 'T');

		}break;
		case CONTROLLER_TYPE_XBOX: {
			s32 pad_read_result = XInputGetState(controller_id, &pad_state);
			if (pad_read_result != ERROR_SUCCESS) {
			}
		}break;
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
		}

		//Find out if the user wants to quit the room
		if (console_impl) {
			if (quit_signal == 'X') {
				SendMsg(client_socket, MESSAGE_REQUEST_ROOM_QUIT);
				Send(client_socket, room_id);
				break;
			}
		}
		else {
			XE_ASSERT(extra_wx_data, "Extra data required if calling from wx\n");

			using AtomicBool = std::atomic<bool>;
			auto exec_thread_flag = static_cast<AtomicBool*>(extra_wx_data);
			if (exec_thread_flag->load()) {
				SendMsg(client_socket, MESSAGE_REQUEST_ROOM_QUIT);
				Send(client_socket, room_id);
				break;
			}
		}

		//Handle audio input
		Log::Debug::Format("Queued audio frames: {}\n", payloads.size());
		{
			std::scoped_lock lk{ payloads_mutex };
			static bool play = false;
			if (payloads.size() >= 15 || play) {
				play = true;
				if (!payloads.empty()) {
					Audio::Payload& payload = payloads.back();
					Audio::RenderAudioFrame(device, payload);
					payloads.pop_back();
				}
			}
		}

		if (std::memcmp(&prev_pad_state.Gamepad, &pad_state.Gamepad, sizeof(XINPUT_GAMEPAD)) != 0) {

			SendMsg(client_socket, MESSAGE_REQUEST_SEND_PAD_DATA);
			Send(client_socket, room_id);
			Send(client_socket, pad_state.Gamepad);

			//TODO handle MESSAGE_ERROR_CLIENT_NOT_CONNECTED
			prev_pad_state = pad_state;
		}

		if (terminate_signal) {
			Log::Format("Host closed the room or its no longer reachable, press anything to close\n");
			break;
		}

		MSG win_msg = {};
		PeekMessage(&win_msg, 0, 0, 0, PM_REMOVE);
		TranslateMessage(&win_msg);
		DispatchMessage(&win_msg);
	}


	recv_thread.join();
	
	if(console_impl)
		quit_thread.join();
	
	device.Release();
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

HWND InitGameWindowContext(Core::FixedBuffer& fixed_buffer, GameWindowData* window_data)
{
	if (!window_data) 
		return nullptr;

	//Extimated compressed value
	window_data->buffer = static_cast<u8*>(fixed_buffer.GetClientSection(CLIENT_ALLOCATIONS_COMPRESSED_SCREEN_BUFFER));

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
		send_buffer_width, send_buffer_height, 
		nullptr, nullptr, instance, window_data);

	if (!game_window) {
		Log::Format("Failed to create game window\n");
		u32 error = GetLastError();
		return nullptr;
	}

	ShowWindow(game_window, SW_SHOW);
	return game_window;
}

void FetchCaptureToGameWindow(HWND& hwnd, GameWindowData* window_data)
{
	XE_ASSERT(window_data, "window_data needs to be defined\n");

	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(hwnd, &ps);

	// Create a bitmap in memory
	BITMAPINFO bmi = { 0 };
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = send_buffer_width;
	bmi.bmiHeader.biHeight = -send_buffer_height; // Use negative height for top-down DIB
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	// Create a memory device context compatible with the window DC
	HDC memDC = CreateCompatibleDC(hdc);
	HBITMAP hBitmap = CreateCompatibleBitmap(hdc, send_buffer_width, send_buffer_height);
	HGDIOBJ gdi_obj = SelectObject(memDC, hBitmap);

	//Uncompress the buffer first
	s32 x, y, comp;
	
	std::unique_lock lk{ window_data->buffer_mutex };
	u8* uncompressed_buf = stbi_load_from_memory(window_data->buffer, window_data->compressed_buffer_size, &x, &y, &comp, 4);
	lk.unlock();

	if (!uncompressed_buf) {
		Log::Format("Stuttering\n");
		return;
	}

	if (SetDIBits(hdc, hBitmap, 0, send_buffer_height, uncompressed_buf, &bmi, DIB_RGB_COLORS) == 0) {
		Log::Format("SetDIBits failed\n");
	}


	// Blit the bitmap to the window
	if (StretchBlt(hdc, 0, 0, window_data->dst_width, window_data->dst_height, memDC, 0, 0,
		send_buffer_width, send_buffer_height, SRCCOPY) == 0) {
		Log::Format("StretchBlt failed\n");
	}


	// Clean up
	stbi_image_free(uncompressed_buf);
	DeleteDC(memDC);
	DeleteObject(hBitmap);

	EndPaint(hwnd, &ps);
}

void DestroyGameWindowContext(HWND& hwnd, WNDCLASS& wnd_class)
{
	DestroyWindow(hwnd);
	UnregisterClass(wnd_class.lpszClassName, wnd_class.hInstance);
}
