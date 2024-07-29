#pragma once
#include "incl.hpp"
#include <vector>
#include <mutex>

struct GameWindowData
{
	std::mutex buffer_mutex;
	std::vector<u8> buffer;
	u32 compressed_buffer_size;
	s32 dst_width;
	s32 dst_height;
	WNDCLASS wnd_class;
};

void QueryRooms(SOCKET client_socket);
u32 QueryDualshockControllers(s32** controller_handles);
u32 QueryXboxControllers(bool slots[XUSER_MAX_COUNT]);
void ClientImplementation(SOCKET client_socket);

HWND InitGameWindowContext(GameWindowData* window_data);
void FetchCaptureToGameWindow(HWND& hwnd, GameWindowData* window_data);
void DestroyGameWindowContext(HWND& hwnd, WNDCLASS& wnd_class);