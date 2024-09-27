#pragma once
#include "incl.hpp"
#include <vector>
#include <mutex>

namespace Core {
	class FixedBuffer;
}

struct GameWindowData
{
	std::mutex buffer_mutex;
	u8* buffer;
	u32 compressed_buffer_size;
	s32 dst_width;
	s32 dst_height;
	WNDCLASS wnd_class;
};

void QueryRooms(SOCKET client_socket, Room::Info** rooms_data, u32* rooms_count);
void PrintQueriedRooms(SOCKET client_socket);

u32 QueryDualshockCount();
u32 QueryXboxCount();
u32 QueryDualshockControllers(s32* controller_handles, bool disconnect_jsl);
u32 QueryXboxControllers(u8* controller_handles);
ControllerData QueryAllControllers();

void ConsoleClientEntry(SOCKET client_socket);
void ExecuteClient(Core::FixedBuffer& fixed_buffer, SOCKET client_socket, ControllerType controller_type,
	u32 controller_id, bool console_impl, void* extra_wx_data);

HWND InitGameWindowContext(Core::FixedBuffer& fixed_buffer, GameWindowData* window_data);
void FetchCaptureToGameWindow(HWND& hwnd, GameWindowData* window_data);
void DestroyGameWindowContext(HWND& hwnd, WNDCLASS& wnd_class);