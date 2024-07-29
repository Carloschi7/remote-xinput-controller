#pragma once
#include "incl.hpp"
#include <condition_variable>
#include <string>

static constexpr u32 max_window_name_length = 128;
static constexpr u32 max_window_enumerations = 128;

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

struct CompressionBuffer
{
	u8* buf;
	u32 buf_size;
	u32 cursor;
};

void TestXboxPad();
void TestDualshock();

void GetCapturedWindowDimensions(const char* process_name, u32* width, u32* height);
void EnumerateWindows(WindowEnumeration* enumerations);
void SendCapturedWindow(SOCKET server_socket, const char* process_name, std::atomic<bool>& run_loop);
u32 GetChangedRegionsCount(u8* curr_buffer, u8* prev_buffer, u32 size);
void GetChangedRegions(u8* curr_buffer, u8* prev_buffer, u32 size, PartialCapture* captures);


SOCKET ConnectToServer(const char* address, USHORT port);
void VigemDeallocate(PVIGEM_CLIENT client, ConnectionInfo* client_connections, u32 count);
void HostImplementation(SOCKET host_socket);
