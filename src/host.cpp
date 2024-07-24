#include "host.hpp"
#include "client.hpp"
#include <string>
#include <chrono>
#include <atomic>

std::atomic<bool> complete_capture_required = true;

void TestXboxPad()
{
	PVIGEM_CLIENT client = vigem_alloc();
	const auto connection = vigem_connect(client);

	PVIGEM_TARGET pad_handle = vigem_target_x360_alloc();
	const auto controller_connection = vigem_target_add(client, pad_handle);

	while (true) {
		XUSB_REPORT report = {};
		report.wButtons |= XUSB_GAMEPAD_A;
		vigem_target_x360_update(client, pad_handle, report);
		Sleep(100);

		report.wButtons = 0;
		vigem_target_x360_update(client, pad_handle, report);
		Sleep(3000);
	}
}

void TestDualshock()
{
	PVIGEM_CLIENT client = vigem_alloc();
	const auto connection = vigem_connect(client);

	PVIGEM_TARGET pad_handle = vigem_target_ds4_alloc();
	const auto controller_connection = vigem_target_add(client, pad_handle);


	while (true) {
		DS4_REPORT report = {};
		DS4_REPORT_INIT(&report);
		report.wButtons |= DS4_BUTTON_CROSS;
		vigem_target_ds4_update(client, pad_handle, report);
		Sleep(100);

		DS4_REPORT_INIT(&report);
		vigem_target_ds4_update(client, pad_handle, report);
		Sleep(3000);
	}
}

void GetCapturedWindowDimensions(const char* process_name, u32* width, u32* height)
{
	if (!width || !height)
		return;

	HWND window = FindWindowA(nullptr, process_name);
	if (!window) {
		std::cout << "Failed to find the window process\n";
		*width = 0;
		*height = 0;
		return;
	}


	RECT window_rect;
	GetWindowRect(window, &window_rect);
	//Added a small padding that accounts for window padding
	*width = window_rect.right - window_rect.left;
	*height = window_rect.bottom - window_rect.top;
}

static BOOL EnumerateWindowsCallback(HWND hwnd, LPARAM lparam) 
{
	char class_name[max_window_name_length] = {};
	auto enumerations = std::bit_cast<WindowEnumeration*>(lparam);

	u32 style = GetWindowLong(hwnd, GWL_STYLE);
	u32 ex_style = GetWindowLong(hwnd, GWL_EXSTYLE);

	if (style & WS_EX_TOOLWINDOW)
		return TRUE;

	if (!(ex_style & WS_OVERLAPPEDWINDOW))
		return TRUE;

	GetWindowTextA(hwnd, class_name, sizeof(class_name));
	
	if (class_name[0] == 0 ||
		std::memcmp(class_name, "Default", sizeof("Default") - 1) == 0 ||
		std::memcmp(class_name, "MSCTFIME", sizeof("MSCTFIME") - 1) == 0) {
		return TRUE;
	}

	//interrupt the enumerations if we have too many windows
	if (enumerations->windows_count >= max_window_enumerations)
		return FALSE;

	std::memcpy(&enumerations->window_names[max_window_name_length * enumerations->windows_count++],
		class_name, sizeof(class_name));
	return TRUE;
}

void EnumerateWindows(WindowEnumeration* enumerations)
{
	EnumWindows(EnumerateWindowsCallback, std::bit_cast<LPARAM>(enumerations));
}

void SendCapturedWindow(SOCKET server_socket, const char* process_name, std::atomic<bool>& run_loop)
{
	HWND window = FindWindowA(nullptr, process_name);
	if (!window) {
		std::cout << "Failed to find the window process\n";
		return;
	}

	HDC window_hdc = GetDC(window);

	RECT window_rect;
	GetWindowRect(window, &window_rect);
	//Added a small padding that accounts for window padding
	s32 width = window_rect.right - window_rect.left;
	s32 height = window_rect.bottom - window_rect.top;

	if (width < 0 || height < 0) {
		std::cout << "Windows is not focused properly, please try again\n";
		return;
	}

	HDC mem_hdc = CreateCompatibleDC(window_hdc);

	bool use_first_buffer = true;
	u8* first_buffer = new u8[width * height * 4];
	u8* second_buffer = new u8[width * height * 4];
	while (run_loop) {
		//Find out if the host resized the winwow
		RECT window_rect;
		GetWindowRect(window, &window_rect);
		//Added a small padding that accounts for window padding
		s32 current_width = window_rect.right - window_rect.left;
		s32 current_height = window_rect.bottom - window_rect.top;

		HBITMAP bitmap = CreateCompatibleBitmap(window_hdc, width, height);
		SelectObject(mem_hdc, bitmap);

		BITMAPINFOHEADER bitmap_header = {};
		bitmap_header.biSize = sizeof(BITMAPINFOHEADER);
		bitmap_header.biWidth = width;
		bitmap_header.biHeight = -height;
		bitmap_header.biPlanes = 1;
		bitmap_header.biBitCount = 32;
		bitmap_header.biCompression = BI_RGB;

		if (current_width != width || current_height != height) {
			SendMsg(server_socket, MESSAGE_INFO_CHANGED_CAPTURED_SCREEN_DIMENSIONS);
			Send(server_socket, current_width);
			Send(server_socket, current_height);
			width = current_width;
			height = current_height;

			delete[] first_buffer;
			delete[] second_buffer;
			//TODO replace this with a preallocated array that is resized troughout the process
			first_buffer = new u8[width * height * 4];
			second_buffer = new u8[width * height * 4];
		}
		else {
			if (!BitBlt(mem_hdc, 0, 0, width, height, window_hdc, 0, 0, SRCCOPY)) {
				std::cout << "BitBlt failed\n";
			}

			u32 buffer_size = width * height * 4;
			if (complete_capture_required) {

				GetDIBits(window_hdc, bitmap, 0, height, first_buffer, (BITMAPINFO*)&bitmap_header, DIB_RGB_COLORS);

				SendMsg(server_socket, MESSAGE_REQUEST_SEND_COMPLETE_CAPTURE);

				Send(server_socket, buffer_size);
				SendBuffer(server_socket, first_buffer, buffer_size);
				complete_capture_required = false;
				use_first_buffer = false;
			}
			else {

				if (use_first_buffer) {
					GetDIBits(window_hdc, bitmap, 0, height, first_buffer, (BITMAPINFO*)&bitmap_header, DIB_RGB_COLORS);

					u32 changed_regions = GetChangedRegionsCount(first_buffer, second_buffer, buffer_size);
					PartialCapture* captures = new PartialCapture[changed_regions];
					GetChangedRegions(first_buffer, second_buffer, buffer_size, captures);
					SendMsg(server_socket, MESSAGE_REQUEST_SEND_PARTIAL_CAPTURE);
					Send(server_socket, changed_regions);
					SendBuffer(server_socket, captures, changed_regions * sizeof(PartialCapture));
					for (u32 i = 0; i < changed_regions; i++) {
						PartialCapture& capture = captures[i];
						SendBuffer(server_socket, first_buffer + capture.begin_index, capture.end_index - capture.begin_index);
					}

					delete[] captures;
					use_first_buffer = false;
				}
				else {
					GetDIBits(window_hdc, bitmap, 0, height, second_buffer, (BITMAPINFO*)&bitmap_header, DIB_RGB_COLORS);

					u32 changed_regions = GetChangedRegionsCount(second_buffer, first_buffer, buffer_size);
					PartialCapture* captures = new PartialCapture[changed_regions];
					GetChangedRegions(second_buffer, first_buffer, buffer_size, captures);
					SendMsg(server_socket, MESSAGE_REQUEST_SEND_PARTIAL_CAPTURE);
					Send(server_socket, changed_regions);
					SendBuffer(server_socket, captures, changed_regions * sizeof(PartialCapture));
					for (u32 i = 0; i < changed_regions; i++) {
						PartialCapture& capture = captures[i];
						SendBuffer(server_socket, second_buffer + capture.begin_index, capture.end_index - capture.begin_index);
					}

					delete[] captures;
					use_first_buffer = true;
				}
			}
		}

		//Trying to reach 60 fps in transmission
		DeleteObject(bitmap);
		Sleep(screen_send_interval_ms);
	}

	delete[] first_buffer;
	delete[] second_buffer;
}

u32 GetChangedRegionsCount(u8* curr_buffer, u8* prev_buffer, u32 size)
{
	if (!curr_buffer || !prev_buffer)
		return 0;

	size /= sizeof(u32);
	u32* curr_buffer_u32 = reinterpret_cast<u32*>(curr_buffer);
	u32* prev_buffer_u32 = reinterpret_cast<u32*>(prev_buffer);

	u32 regions = 0;
	bool changed_region = false;
	for (u32 i = 0; i < size; i++) {
		if (curr_buffer_u32[i] != prev_buffer_u32[i] && !changed_region) {
			regions++;
			changed_region = true;
		}

		if (curr_buffer_u32[i] == prev_buffer_u32[i] && changed_region)
			changed_region = false;
	}

	return regions;
}

void GetChangedRegions(u8* curr_buffer, u8* prev_buffer, u32 size, PartialCapture* captures)
{
	if (!curr_buffer || !prev_buffer || !captures)
		return;

	size /= sizeof(u32);
	u32* curr_buffer_u32 = reinterpret_cast<u32*>(curr_buffer);
	u32* prev_buffer_u32 = reinterpret_cast<u32*>(prev_buffer);

	u32 regions = 0;
	bool changed_region = false;
	for (u32 i = 0; i < size; i++) {
		if (curr_buffer_u32[i] != prev_buffer_u32[i] && !changed_region) {
			PartialCapture* capture = &captures[regions];
			capture->begin_index = i * 4;
			//Avoid bugs if buffer ends
			capture->end_index = i * 4;
			changed_region = true;
		}

		if (curr_buffer_u32[i] == prev_buffer_u32[i] && changed_region) {
			PartialCapture* capture = &captures[regions++];
			capture->end_index = i * 4;
			changed_region = false;
		}
	}
}

SOCKET ConnectToServer(const char* address, USHORT port)
{
	SOCKET connecting_socket = INVALID_SOCKET;
	WSADATA wsaData;
	s32 result = 0;

	s32 wsa_startup = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (wsa_startup != 0) {
		std::cout << "WSAStartup failed: " << wsa_startup;
		return INVALID_SOCKET;
	}

	struct sockaddr_in serverAddress;
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons(port);
	inet_pton(AF_INET, address, &serverAddress.sin_addr);

	connecting_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr* socket_info = (sockaddr*)&serverAddress;
	result = connect(connecting_socket, socket_info, sizeof(sockaddr));
	if (result == -1) {
		closesocket(connecting_socket);
		return INVALID_SOCKET;
	}

	return connecting_socket;
}

void VigemDeallocate(PVIGEM_CLIENT client, ConnectionInfo* client_connections, u32 count)
{
	if (client_connections) {
		for (u32 i = 0; i < count; i++) {
			if (!client_connections[i].connected)
				continue;

			vigem_target_remove(client, client_connections[i].pad_handle);
			vigem_target_free(client_connections[i].pad_handle);
		}

		delete[] client_connections;
	}
	vigem_disconnect(client);
	vigem_free(client);
}



void HostImplementation(SOCKET host_socket)
{
	u32 physical_pads = QueryDualshockControllers(nullptr) + QueryXboxControllers(nullptr);
	u32 virtual_pads = 0;

	std::cout << "Numbers of xbox pads to connect (" << XUSER_MAX_COUNT - physical_pads << " slots available)\n";
	std::cin >> virtual_pads;

	if (virtual_pads > XUSER_MAX_COUNT - physical_pads) {
		std::cout << "Not enough space\n";
		return;
	}

	Room::Info room_info;
	room_info.current_pads = 0;
	room_info.max_pads = virtual_pads;
	{
		std::string room_name;
		std::cout << "Insert room name:\n";
		std::cin >> room_name;
		if (room_name.size() >= sizeof(room_info.name)) {
			std::memcpy(room_info.name, "unknown", sizeof("unknown"));
		}
		else {
			std::memcpy(room_info.name, room_name.c_str(), room_name.size());
			room_info.name[room_name.size()] = 0;
		}
	}

	PVIGEM_CLIENT client = vigem_alloc();
	const auto connection = vigem_connect(client);

	if (!VIGEM_SUCCESS(connection))
	{
		std::cout << "To run the server u need to have vigem installed\n";
		VigemDeallocate(client, nullptr, 0);
		return;
	}

	char selected_window_name[max_window_name_length] = {};
	{
		WindowEnumeration* enumeration = new WindowEnumeration;
		ASSERT(enumeration);
		ZeroMemory(enumeration, sizeof(WindowEnumeration));

		EnumerateWindows(enumeration);
		u32 window_choice;
		
		auto print_single_enumeration = [&](u32 offset) {
			std::cout << offset << ": {";
			for (u32 j = 0; j < max_window_name_length && 
				enumeration->window_names[offset * max_window_name_length + j] != 0;
				j++) {
				std::cout << enumeration->window_names[offset * 128 + j];
			}
			std::cout << "}\n";
		};

		for (u32 i = 0; i < enumeration->windows_count; i++) {
			print_single_enumeration(i);
		}

		do {
			std::cout << "Select a valid window to capture from:\n";
			std::cin >> window_choice;
		} while (window_choice >= enumeration->windows_count);
		std::memcpy(selected_window_name, &enumeration->window_names[window_choice * max_window_name_length], max_window_name_length);

		delete enumeration;
	}

	ConnectionInfo* client_connections = new ConnectionInfo[virtual_pads];

	SendMsg(host_socket, MESSAGE_REQUEST_ROOM_CREATE);
	Send(host_socket, room_info);
	
	{
		u32 width, height;
		GetCapturedWindowDimensions(selected_window_name, &width, &height);
		Send(host_socket, width);
		Send(host_socket, height);
	}

	std::atomic<char> quit_signal;
	std::atomic<bool> run_loops = true;

	std::thread host_input_thd([&quit_signal]() {
		while (true) {
			char ch; std::cin >> ch;
			if (ch == 'X') { quit_signal = ch; break; }
		} });
	std::cout << "Room created {X to close it}\n";

	std::thread capture_thread;

	while (run_loops) {

		Message msg = ReceiveMsg(host_socket);
		switch (msg) {
		case MESSAGE_INFO_SERVER_PING: {
			//Closing the room
			if (quit_signal.load() == 'X') {
				std::cout << "Closing room...\n";
				SendMsg(host_socket, MESSAGE_INFO_ROOM_CLOSING);
				run_loops = false;
			}
		}break;
		case MESSAGE_INFO_CLIENT_JOINING_ROOM: {
			s32 index = -1;
			for (u32 i = 0; i < XUSER_MAX_COUNT; i++) {
				if (!client_connections[i].connected) {
					index = i;
					break;
				}
			}

			//This happens only if the server allows a connection when the room is full
			//something in the server code is not quite right
			ASSERT(index != -1);

			ConnectionInfo& connection = client_connections[index];
			connection.pad_handle = vigem_target_x360_alloc();
			connection.connected = true;
			const auto controller_connection = vigem_target_add(client, connection.pad_handle);

			if (!VIGEM_SUCCESS(controller_connection)) {
				std::cout << "ViGEm Bus connection failed with error code: " << std::hex << controller_connection;
				SendMsg(host_socket, MESSAGE_ERROR_HOST_COULD_NOT_ALLOCATE_PAD);
			}
			else {
				SendMsg(host_socket, MESSAGE_INFO_PAD_ALLOCATED);
				//Initialize also the thread
				if (!capture_thread.joinable())
					capture_thread = std::thread([&]() { SendCapturedWindow(host_socket, "Binding of Isaac: Repentance", run_loops); });
			}

			std::cout << "Connection found!!" << std::endl;
			complete_capture_required = true;
		} break;
		case MESSAGE_INFO_CLIENT_DISCONNECTED: {
			u32 client_id;
			Receive(host_socket, &client_id);
			std::cout << "Client " << client_id << " disconnected\n";
			ASSERT(client_connections[client_id].connected);
			client_connections[client_id].connected = false;
			vigem_target_remove(client, client_connections[client_id].pad_handle);
			vigem_target_free(client_connections[client_id].pad_handle);
		}break;
		case MESSAGE_REQUEST_SEND_PAD_DATA: {
			PadSignal signal;
			u32 error_code = Receive(host_socket, &signal);

			vigem_target_x360_update(client, client_connections[signal.pad_number].pad_handle, *reinterpret_cast<XUSB_REPORT*>(&signal.pad_state));
			std::cout << "Signal from pad " << signal.pad_number << "\n";
			std::cout << signal.pad_state.wButtons << std::endl;
		}break;
		}
	}

	host_input_thd.join();
	if(capture_thread.joinable())
		capture_thread.join();
	VigemDeallocate(client, client_connections, virtual_pads);
}
