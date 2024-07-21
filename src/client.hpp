#pragma once
#include "incl.hpp"
#include <vector>

struct GameWindowData
{
	std::vector<u8> buffer;
	s32 src_width;
	s32 src_height;
	s32 dst_width;
	s32 dst_height;
	bool on_resize = false;
};

void QueryRooms(SOCKET client_socket);
u32 QueryDualshockControllers(s32** controller_handles);
u32 QueryXboxControllers(bool slots[XUSER_MAX_COUNT]);
void ClientImplementation(SOCKET client_socket);

HWND InitGameWindowContext(GameWindowData* window_data);
void FetchCaptureToGameWindow(HWND& hwnd, GameWindowData* window_data);
void DestroyGameWindowContext(HWND& hwnd);