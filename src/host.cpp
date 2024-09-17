#include "host.hpp"
#include "client.hpp"
#include "mem.hpp"
#include "audio.hpp"
#include <string>
#include <chrono>
#include <atomic>
#include <algorithm>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define __STDC_LIB_EXT1__
#include <stbi_image_write.h>

std::atomic<bool> full_capture_needed = true;
const u32 buffer_length_in_seconds = 1;

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

void SendCapturedData(SOCKET server_socket, const char* process_name, Core::FixedBuffer& fixed_buffer, std::atomic<bool>& run_loop)
{
	HWND window = FindWindowA(nullptr, process_name);
	if (!window) {
		Log::Format("Failed to find the window process\n");
		return;
	}

	HDC window_hdc = GetDC(window);
	HDC mem_hdc = CreateCompatibleDC(window_hdc);
	HBITMAP bitmap = CreateCompatibleBitmap(window_hdc, send_buffer_width, send_buffer_height);
	SelectObject(mem_hdc, bitmap);

	// Prepare the BITMAPINFOHEADER structure for GetDIBits
	BITMAPINFOHEADER bitmap_header;
	bitmap_header.biSize = sizeof(BITMAPINFOHEADER);
	bitmap_header.biWidth = send_buffer_width;
	bitmap_header.biHeight = -send_buffer_height;
	bitmap_header.biPlanes = 1;
	bitmap_header.biBitCount = 32;
	bitmap_header.biCompression = BI_RGB;
	bitmap_header.biSizeImage = 0;

	CompressionBuffer compressed_buf = {}, prev_compressed_buf = {};

	compressed_buf.buf_size = (send_buffer_width * send_buffer_height * 4) / 10;
	prev_compressed_buf.buf_size = compressed_buf.buf_size;

	compressed_buf.buf = static_cast<u8*>(fixed_buffer.GetHostSection(HOST_ALLOCATIONS_COMPRESSED_BUF));
	prev_compressed_buf.buf = static_cast<u8*>(fixed_buffer.GetHostSection(HOST_ALLOCATIONS_PREV_COMPRESSED_BUF));
	u8* uncompressed_buf = static_cast<u8*>(fixed_buffer.GetHostSection(HOST_ALLOCATIONS_UNCOMPRESSED_BUF));

	auto compression_func = [](void* context, void* data, int size) 
	{
		CompressionBuffer* raw_buf = (CompressionBuffer*)context;
		XE_ASSERT(raw_buf->cursor + size < raw_buf->buf_size, "Compressed buffer size too small\n");
		std::memcpy(raw_buf->buf + raw_buf->cursor, data, size);
		raw_buf->cursor += size;
	};

	SetStretchBltMode(mem_hdc, HALFTONE);

	std::mutex payloads_mutex;
	std::list<Audio::Payload> payloads;

	std::thread audio_capture_thread = std::thread([&]() { Sleep(4000); 
		CaptureAudio(payloads, payloads_mutex, run_loop); });

	while (run_loop) {
		RECT window_rect;
		GetWindowRect(window, &window_rect);
		s32 width = window_rect.right - window_rect.left;
		s32 height = window_rect.bottom - window_rect.top;

		//Video capture
		StretchBlt(mem_hdc, 0, 0, send_buffer_width, send_buffer_height, window_hdc, 0, 0, width, height, SRCCOPY);
		s32 rows_parsed = GetDIBits(mem_hdc, bitmap, 0, send_buffer_height, uncompressed_buf, (BITMAPINFO*)&bitmap_header, DIB_RGB_COLORS);
		XE_ASSERT(rows_parsed == send_buffer_height, "Not all image rows parsed, intended: {}, parsed: {}\n", rows_parsed, send_buffer_height);

		compressed_buf.cursor = 0;
		stbi_write_jpg_to_func(compression_func, &compressed_buf, send_buffer_width, send_buffer_height, 4, uncompressed_buf, 50);
		
		if (full_capture_needed) {
			SendMsg(server_socket, MESSAGE_REQUEST_SEND_COMPLETE_VIDEO_CAPTURE);
			Send(server_socket, compressed_buf.cursor);
			SendBuffer(server_socket, compressed_buf.buf, compressed_buf.cursor);

			prev_compressed_buf.cursor = compressed_buf.cursor;
			std::swap(prev_compressed_buf.buf, compressed_buf.buf);
			full_capture_needed = false;
		}
		else {
			u32 curr_buf_size = compressed_buf.cursor;
			u32 diff_point = GetChangedRegionBegin(compressed_buf.buf, prev_compressed_buf.buf, curr_buf_size);
			if (curr_buf_size != prev_compressed_buf.cursor || diff_point != UINT32_MAX) {
				SendMsg(server_socket, MESSAGE_REQUEST_SEND_PARTIAL_VIDEO_CAPTURE);
				Send(server_socket, diff_point);
				Send(server_socket, curr_buf_size);
				SendBuffer(server_socket, compressed_buf.buf + diff_point, curr_buf_size - diff_point);

				prev_compressed_buf.cursor = curr_buf_size;
				std::swap(prev_compressed_buf.buf, compressed_buf.buf);
			}
		}
		//Audio send
		std::unique_lock lk(payloads_mutex);
		if (payloads.size() >= audio_packets_per_single_send) {
			SendMsg(server_socket, MESSAGE_REQUEST_SEND_AUDIO_CAPTURE);
			u32 packet_size = Audio::unit_packet_size_in_bytes * audio_packets_per_single_send;
			Send(server_socket, packet_size);

			//Send two frames at a time
			u32 single_send_size = packet_size / audio_packets_per_single_send;
			for (u32 i = 0; i < 10; i++) {
				Audio::Payload& payload = payloads.back();
				SendBuffer(server_socket, payload.data, single_send_size);
				payloads.pop_back();
			}
		}
		lk.unlock();

		Sleep(screen_send_interval_ms);
	}

	audio_capture_thread.join();
	DeleteDC(mem_hdc);
	DeleteObject(bitmap);
}

void CaptureAudio(std::list<Audio::Payload>& payloads, std::mutex& payloads_mutex, std::atomic<bool>& run_loop)
{
	Audio::Device device;
	Audio::InitDevice(&device, true);

	Audio::Payload first_frame = {}, second_frame = {};
	while (run_loop) {
		WaitForSingleObject(device.event_handle, INFINITE);
		Audio::CaptureAudioFrame(device, first_frame, second_frame);
		if (first_frame.initialized) {
			std::scoped_lock lk{ payloads_mutex };
			payloads.push_front(first_frame);

			if (second_frame.initialized) {
				payloads.push_front(second_frame);
			}
		}

	}

	device.Release();
}

u32 GetChangedRegionBegin(u8* curr_buffer, u8* prev_buffer, u32 size)
{
	if (!curr_buffer || !prev_buffer)
		return 0;

	for (u32 i = 0; i < size; i++) {
		if (curr_buffer[i] != prev_buffer[i]) {
			return i;
		}
	}

	return UINT32_MAX;
}

SOCKET ConnectToServer(const char* address, USHORT port)
{
	SOCKET connecting_socket = INVALID_SOCKET;
	WSADATA wsaData;
	s32 result = 0;

	s32 wsa_startup = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (wsa_startup != 0) {
		Log::Format("WSAStartup failed: {}\n", wsa_startup);
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
	}
	vigem_disconnect(client);
	vigem_free(client);
}



void HostImplementation(SOCKET host_socket)
{
	Core::FixedBuffer fixed_buffer(FIXED_BUFFER_TYPE_HOST);
	fixed_buffer.ResetMemory();
	u32 physical_pads = QueryDualshockCount() + QueryXboxCount();
	u32 virtual_pads = 0;

	Log::Format("Numbers of xbox pads to connect ({} slots available)\n", XUSER_MAX_COUNT - physical_pads);
	std::cin >> virtual_pads;

	if (virtual_pads > XUSER_MAX_COUNT - physical_pads) {
		Log::Format("Not enough space\n");
		return;
	}

	Room::Info room_info;
	room_info.current_pads = 0;
	room_info.max_pads = virtual_pads;
	{
		std::string room_name;
		Log::Format("Insert room name:\n");
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
		Log::Format("To run the server u need to have vigem installed\n");
		VigemDeallocate(client, nullptr, 0);
		return;
	}

	char selected_window_name[max_window_name_length] = {};
	{
		auto enumeration = static_cast<WindowEnumeration*>(fixed_buffer.GetHostSection(HOST_ALLOCATIONS_WINDOW_ENUM));
		EnumerateWindows(enumeration);

		u32 window_choice;
		{
			char cur_window_name[max_window_name_length];
			for (u32 i = 0; i < enumeration->windows_count; i++) {
				std::memcpy(cur_window_name, &enumeration->window_names[i * max_window_name_length], max_window_name_length);
				Log::Format("{}: {}\n", i, cur_window_name);
			}
		}

		do {
			Log::Format("Select a valid window to capture from:\n");
			std::cin >> window_choice;
		} while (window_choice >= enumeration->windows_count);
		std::memcpy(selected_window_name, &enumeration->window_names[window_choice * max_window_name_length], max_window_name_length);
	}

	auto client_connections = static_cast<ConnectionInfo*>(fixed_buffer.GetHostSection(HOST_ALLOCATIONS_CLIENT_CONNECTIONS));
	//ZeroMemory(client_connections, g_host_allocations_offsets[HOST_ALLOCATIONS_CLIENT_CONNECTIONS]);
	SendMsg(host_socket, MESSAGE_REQUEST_ROOM_CREATE);
	Send(host_socket, room_info);

	std::atomic<char> quit_signal;
	std::atomic<bool> run_loops = true;

	std::thread host_input_thd([&quit_signal]() {
		while (true) {
			char ch; std::cin >> ch;
			if (ch == 'X') { quit_signal = ch; break; }
		} });
	Log::Format("Room created (X to close it)\n");

	std::thread video_capture_thread;

	while (run_loops) {

		Message msg = ReceiveMsg(host_socket);
		switch (msg) {
		case MESSAGE_INFO_SERVER_PING: {
			//Closing the room
			if (quit_signal.load() == 'X') {
				Log::Format("Closing room...\n");
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
			XE_ASSERT(index != -1, "The server allowed a connection when there was no space, check that\n");

			ConnectionInfo& connection = client_connections[index];
			connection.pad_handle = vigem_target_x360_alloc();
			connection.connected = true;
			const auto controller_connection = vigem_target_add(client, connection.pad_handle);

			if (!VIGEM_SUCCESS(controller_connection)) {
				Log::Format("ViGEm Bus connection failed with error code: {:x}\n", static_cast<u32>(controller_connection));
				SendMsg(host_socket, MESSAGE_ERROR_HOST_COULD_NOT_ALLOCATE_PAD);
			}
			else {
				SendMsg(host_socket, MESSAGE_INFO_PAD_ALLOCATED);
				//Initialize also the thread
				if (!video_capture_thread.joinable())
					video_capture_thread = std::thread([&]() { SendCapturedData(host_socket, selected_window_name, fixed_buffer, run_loops); });

			}

			Log::Format("Connection found!!\n");
			full_capture_needed = true;
		} break;
		case MESSAGE_INFO_CLIENT_DISCONNECTED: {
			u32 client_id;
			Receive(host_socket, &client_id);
			Log::Format("Client {} disconnected\n", client_id);
			XE_ASSERT(client_connections[client_id].connected, "Client needs to be connected here\n");
			client_connections[client_id].connected = false;
			vigem_target_remove(client, client_connections[client_id].pad_handle);
			vigem_target_free(client_connections[client_id].pad_handle);
		}break;
		case MESSAGE_REQUEST_SEND_PAD_DATA: {
			PadSignal signal;
			u32 error_code = Receive(host_socket, &signal);

			vigem_target_x360_update(client, client_connections[signal.pad_number].pad_handle, *reinterpret_cast<XUSB_REPORT*>(&signal.pad_state));
			Log::Debug::Format("Signal from pad {}\n", signal.pad_number);
			Log::Debug::Format("{}\n", signal.pad_state.wButtons);
		}break;
		}
	}

	host_input_thd.join();
	if(video_capture_thread.joinable())
		video_capture_thread.join();
	VigemDeallocate(client, client_connections, virtual_pads);
}