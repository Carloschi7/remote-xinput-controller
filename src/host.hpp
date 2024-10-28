#pragma once
#include "incl.hpp"
#include <condition_variable>
#include <string>
#include <list>

namespace Core {
	class FixedBuffer;
}

namespace Audio {
	struct Device;
	struct Payload;
}

struct CompressionBuffer
{
	u8* buf;
	u32 buf_size;
	u32 cursor;
};

void TestXboxPad();
void TestDualshock();

void EnumerateWindows(WindowEnumeration* enumerations);
void SendCapturedData(SOCKET server_socket, const char* process_name, Core::FixedBuffer& fixed_buffer, std::atomic<bool>& run_loop);
void CaptureAudio(std::list<Audio::Payload>& payloads, std::mutex& payloads_mutex, std::atomic<bool>& run_loop);
u32 GetChangedRegionBegin(u8* curr_buffer, u8* prev_buffer, u32 size);

SOCKET ConnectToServer(const char* address, USHORT port);
bool ValidateIpAddress(const char* address);
void VigemDeallocate(PVIGEM_CLIENT client, ConnectionInfo* client_connections, u32 count);
void HostImplementation(SOCKET host_socket);
//Room::Info is copied to make sure data lifetime is kept intact during wx runtime
void ExecuteHost(Core::FixedBuffer& fixed_buffer, SOCKET host_socket, char* selected_window_name, Room::Info room_info,
    PVIGEM_CLIENT client, bool console_impl, void* extra_wx_data);
