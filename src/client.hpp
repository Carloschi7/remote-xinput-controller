#pragma once
#include "incl.hpp"
#include <vector>

struct GameWindowData
{
	std::vector<u8> buffer;
	HWND game_window;
	u32 width;
	u32 height;

	void Clear() 
	{
		buffer.clear();
		game_window = 0;
		width = 0;
		height = 0;
	}
};

void QueryRooms(SOCKET client_socket);
u32 QueryDualshockControllers(s32** controller_handles);
u32 QueryXboxControllers(bool slots[XUSER_MAX_COUNT]);
void ClientImplementation(SOCKET client_socket);

void InitGameWindowContext(GameWindowData* window_data);
void FetchCaptureToGameWindow(HWND& hwnd, void* buffer, s32 window_width, s32 window_height);
void DestroyGameWindowContext(HWND& hwnd);