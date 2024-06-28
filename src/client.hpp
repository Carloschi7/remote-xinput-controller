#pragma once
#include "incl.hpp"

void QueryRooms(SOCKET client_socket);
u32 QueryDualshockControllers(s32** controller_handles);
u32 QueryXboxControllers(bool slots[XUSER_MAX_COUNT]);
void ClientImplementation(SOCKET client_socket);