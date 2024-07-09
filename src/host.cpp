#include "host.hpp"
#include "client.hpp"
#include <string>
#include <chrono>

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
	s32 width = window_rect.right - window_rect.left - 20;
	s32 height = window_rect.bottom - window_rect.top - 40;

	HDC mem_hdc = CreateCompatibleDC(window_hdc);
	HBITMAP bitmap = CreateCompatibleBitmap(window_hdc, width, height);

	SelectObject(mem_hdc, bitmap);

	BITMAPINFOHEADER bitmap_header = {};
	bitmap_header.biSize = sizeof(BITMAPINFOHEADER);
	bitmap_header.biWidth = width;
	bitmap_header.biHeight = -height;
	bitmap_header.biPlanes = 1;
	bitmap_header.biBitCount = 32;
	bitmap_header.biCompression = BI_RGB;

	u8* buffer = new u8[width * height * 4];
	while (run_loop) {
#ifdef DEBUG_BUILD
		u64 t1 = GetTickCount64();
#endif
		if (!BitBlt(mem_hdc, 0, 0, width, height, window_hdc, 0, 0, SRCCOPY)) {
			std::cout << "BitBlt failed\n";
		}
		GetDIBits(window_hdc, bitmap, 0, height, buffer, (BITMAPINFO*)&bitmap_header, DIB_RGB_COLORS);


		SendMsg(server_socket, MESSAGE_REQUEST_SEND_CAPTURED_SCREEN);
		u32 buffer_size = width * height * 4;
		Send(server_socket, buffer_size);
		//TODO too slow, make it faster
		SendBuffer(server_socket, buffer, buffer_size);

#ifdef DEBUG_BUILD
		u64 t2 = GetTickCount64();
		std::cout << "First capture lasted " << t2 - t1 << " milliseconds\n";
#endif
		//Trying to reach 30 fps in transmission
		Sleep(1000 / 30);
	}

	delete[] buffer;
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

	ConnectionInfo* client_connections = new ConnectionInfo[virtual_pads];

	SendMsg(host_socket, MESSAGE_REQUEST_ROOM_CREATE);
	Send(host_socket, room_info);

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
	capture_thread.join();
	VigemDeallocate(client, client_connections, virtual_pads);
}
