#include "server.hpp"
#include <iostream>

static constexpr u32 initial_allocated_room_count = 32;

u32 AllocateNewSyncPrimitive(ServerData* server_data)
{
	std::unique_lock lk{ server_data->heap_mtx };
	if (!server_data->sync_primitive_heap_ptr) {
		server_data->sync_primitive_heap_ptr = new SyncPrimitiveHeap[initial_allocated_room_count];
		ASSERT(server_data->sync_primitive_heap_count);
		server_data->sync_primitive_heap_count = initial_allocated_room_count;
	}

	SyncPrimitiveHeap*& heap_ptr = server_data->sync_primitive_heap_ptr;
	u32& heap_count = server_data->sync_primitive_heap_count;
	for (u32 i = 0; i < heap_count; i++) {
		if (!heap_ptr[i].slot_taken) {
			heap_ptr[i].slot_taken = true;
			return i;
		}
	}

	//Need to reallocate to create more space
	while (server_data->borrows != 0) {
		lk.unlock();
		Sleep(20);
		lk.lock();
	}

	SyncPrimitiveHeap* new_ptr = new SyncPrimitiveHeap[heap_count * 2];
	ASSERT(new_ptr);
	std::memcpy(new_ptr, heap_ptr, heap_count * sizeof(SyncPrimitiveHeap));
	delete[] heap_ptr;
	heap_ptr = new_ptr;
	//Should always be true
	ASSERT(!heap_ptr[heap_count].slot_taken);
	u32 index = heap_count;
	heap_count *= 2;

	return index;
}

Room::SyncPrimitives* LockSyncPrimitive(ServerData* server_data, u32 index)
{
	std::scoped_lock lk{ server_data->heap_mtx };
	SyncPrimitiveHeap* heap_ptr = server_data->sync_primitive_heap_ptr;
	u32& heap_count = server_data->sync_primitive_heap_count;
	if (index < heap_count && heap_ptr[index].slot_taken) {
		server_data->borrows++;
		return &heap_ptr[index].data;
	}

	return nullptr;
}

void UnlockSyncPrimitive(ServerData* server_data, u32 index)
{
	std::scoped_lock lk{ server_data->heap_mtx };
	SyncPrimitiveHeap* heap_ptr = server_data->sync_primitive_heap_ptr;
	u32& heap_count = server_data->sync_primitive_heap_count;
	if (index < heap_count && heap_ptr[index].slot_taken) {
		server_data->borrows--;
	}
}

void FreeSyncPrimitive(ServerData* server_data, u32 index)
{
	std::scoped_lock lk{ server_data->heap_mtx };
	SyncPrimitiveHeap* heap_ptr = server_data->sync_primitive_heap_ptr;
	u32& heap_count = server_data->sync_primitive_heap_count;
	if (index < heap_count && heap_ptr[index].slot_taken) {
		heap_ptr[index].slot_taken = false;
	}
}

SOCKET SetupServerSocket(USHORT port)
{
	SOCKET host_socket = INVALID_SOCKET;
	WSADATA wsaData;
	s32 result = 0;

	s32 wsa_startup = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (wsa_startup != 0) {
		std::cout << "WSAStartup failed: " << wsa_startup;
		return INVALID_SOCKET;
	}

	sockaddr_in server_address = {};
	server_address.sin_family = AF_INET;
	server_address.sin_addr.s_addr = INADDR_ANY;
	server_address.sin_port = htons(port);

	host_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	result = bind(host_socket, (sockaddr*)&server_address, sizeof(server_address));
	if (result == SOCKET_ERROR) {
		closesocket(host_socket);
		WSACleanup();
		return INVALID_SOCKET;
	}

	if (listen(host_socket, SOMAXCONN) == SOCKET_ERROR) {
		std::cout << "listen failed with error: " << WSAGetLastError() << '\n';
		closesocket(host_socket);
		WSACleanup();
		return INVALID_SOCKET;
	}

	return host_socket;
}



void StartServer()
{
	SOCKET server_socket = SetupServerSocket(20000);

	std::vector<std::thread> server_threads;
	ServerData server_data;

	server_threads.emplace_back(&PingRooms, &server_data);
	while (true) {
		SOCKET new_host = accept(server_socket, nullptr, nullptr);
		server_threads.emplace_back(&HandleConnection, &server_data, new_host);
	}

	for (auto& thd : server_threads)
		if (thd.joinable())
			thd.join();
}

void PingRooms(ServerData* server_data)
{
	//Send some ping request every once in a while, this is mainly done
	//so that the main host thread can respond also to the host input instead
	//of activating only when some pad data is sent
	while (true) {
		std::unique_lock rooms_lock{ server_data->rooms_mutex };
		auto& rooms = server_data->rooms;
		for (u32 i = 0; i < rooms.size(); i++) {
			SendMsg(rooms[i].host_socket, MESSAGE_INFO_SERVER_PING);
		}
		rooms_lock.unlock();
		Sleep(1000);
	}

}

void HandleConnection(ServerData* server_data, SOCKET other_socket)
{
	bool is_this_client_hosting = false;
	auto& rooms = server_data->rooms;

	while (true) {
		//We actually care about the possibility of errors here
		Message msg;
		bool correct_recv = Receive(other_socket, &msg);
		std::unique_lock rooms_lock{ server_data->rooms_mutex };

		if (!correct_recv) {

			if (is_this_client_hosting) {
				s32 socket_room = -1;
				for (u32 i = 0; i < rooms.size(); i++) {
					if (other_socket == rooms[i].host_socket) {
						socket_room = static_cast<s32>(i);
						break;
					}
				}

				//Should always be true
				if (socket_room != -1) {
					FreeSyncPrimitive(server_data, rooms[socket_room].sync_primitives_index);
					rooms.erase(rooms.begin() + socket_room);
				}
			}
			else {
				for (u32 i = 0; i < rooms.size(); i++) {
					for (u32 j = 0; j < 4; j++) {
						auto& sock_info = rooms[i].connected_sockets[j];
						if (other_socket == sock_info.sock) {
							sock_info.connected = false;
							rooms[i].info.current_pads--;
							SendMsg(rooms[i].host_socket, MESSAGE_INFO_CLIENT_DISCONNECTED);
							//The connected index from the server should match the one of the host
							Send(rooms[i].host_socket, j);
							break;
						}
					}

				}
			}

			break;
		}

		switch (msg) {
		case MESSAGE_REQUEST_ROOM_CREATE: {
			is_this_client_hosting = true;

			Room::Info new_room_info;
			Receive(other_socket, &new_room_info);
			Receive(other_socket, &new_room_info.host_window_width);
			Receive(other_socket, &new_room_info.host_window_height);
			Room& room = rooms.emplace_back();
			room.id = server_data->id_generator++;
			room.info = new_room_info;
			room.host_socket = other_socket;
			//TODO ugly, change this
			room.sync_primitives_index = AllocateNewSyncPrimitive(server_data);
		} break;

		case MESSAGE_REQUEST_ROOM_JOIN: {
			is_this_client_hosting = false;

			u32 room_to_join;
			Receive(other_socket, &room_to_join);

			if (room_to_join >= rooms.size()) {
				SendMsg(other_socket, MESSAGE_ERROR_INDEX_OUT_OF_BOUNDS);
				break;
			}

			Room& host_room = rooms[room_to_join];
			if (host_room.info.current_pads < host_room.info.max_pads) {
				host_room.info.current_pads++;

				SendMsg(host_room.host_socket, MESSAGE_INFO_CLIENT_JOINING_ROOM);

				Message host_msg;
				{
					//Ask for the host thread if there are any issues during the connecting phase
					Room::SyncPrimitives* sync_primitives = LockSyncPrimitive(server_data, host_room.sync_primitives_index);
					std::unique_lock lk{ sync_primitives->mtx };
					//Always unlock the host_room lock to allow the host thread to send the connecting message
					rooms_lock.unlock();
					sync_primitives->notify_cv.wait(lk);
					rooms_lock.lock();
					host_msg = host_room.connecting_message;
					UnlockSyncPrimitive(server_data, host_room.sync_primitives_index);
				}

				Message response;
				if (host_msg == MESSAGE_ERROR_NONE) {

					u32 client_slot = 0;
					while (client_slot < XUSER_MAX_COUNT && host_room.connected_sockets[client_slot].connected)
						client_slot++;

					host_room.connected_sockets[client_slot].sock = other_socket;
					host_room.connected_sockets[client_slot].connected = true;

					SendMsg(other_socket, MESSAGE_ERROR_NONE);
					Send(other_socket, host_room.id);
					//To display the game in real time if it is enabled
					//Send(other_socket, host_room.info.host_window_width);
					//Send(other_socket, host_room.info.host_window_height);
				}
				else {
					SendMsg(other_socket, MESSAGE_ERROR_HOST_COULD_NOT_ALLOCATE_PAD);
				}
			}
			else {
				SendMsg(other_socket, MESSAGE_ERROR_ROOM_AT_FULL_CAPACITY);
			}
		} break;
		case MESSAGE_REQUEST_ROOM_QUIT: {
			if (is_this_client_hosting)
				break;

			u64 room_id;
			Receive(other_socket, &room_id);

			u32 room_index = 0;
			bool found_room = false;

			for (u32 i = 0; i < rooms.size(); i++) {
				if (rooms[i].id == room_id) {
					room_index = i;
					found_room = true;
					break;
				}
			}

			if (found_room) {
				Room& current_room = rooms[room_index];
				for (u32 i = 0; i < XUSER_MAX_COUNT; i++) {
					if (current_room.connected_sockets[i].sock == other_socket) {
						current_room.connected_sockets[i].connected = false;
						current_room.info.current_pads--;

						SendMsg(current_room.host_socket, MESSAGE_INFO_CLIENT_DISCONNECTED);
						Send(current_room.host_socket, i);
					}
				}
			}
			//Notify the listening thread of the client it needs to shut down
			SendMsg(other_socket, MESSAGE_REQUEST_ROOM_QUIT);
			
		}break;
		case MESSAGE_REQUEST_ROOM_QUERY: {
			u32 room_count = rooms.size();
			Send(other_socket, room_count);
			for (u32 i = 0; i < rooms.size(); i++) {
				Send(other_socket, rooms[i].info);
			}
		} break;

		case MESSAGE_REQUEST_SEND_PAD_DATA: {
			if (is_this_client_hosting)
				break;

			u32 room_index = 0, client_slot;
			u64 room_id;
			XINPUT_GAMEPAD pad_state;
			Receive(other_socket, &room_id);
			Receive(other_socket, &pad_state);

			bool found_room = false, found_match = false;
			for (u32 i = 0; i < rooms.size(); i++) {
				if (rooms[i].id == room_id) {
					room_index = i;
					found_room = true;
					break;
				}
			}

			if (found_room) {
				for (u32 i = 0; i < 4; i++) {
					if (rooms[room_index].connected_sockets[i].sock == other_socket) {
						found_match = true;
						client_slot = i;
						break;
					}
				}

				//The socket is actually connected to the host_room
				if (found_match) {
					PadSignal pad_signal;
					pad_signal.pad_number = client_slot;
					pad_signal.pad_state = pad_state;
					SendMsg(other_socket, MESSAGE_ERROR_NONE);
					SendMsg(rooms[room_index].host_socket, MESSAGE_REQUEST_SEND_PAD_DATA);
					Send(rooms[room_index].host_socket, pad_signal);
				}
				else {
					SendMsg(other_socket, MESSAGE_ERROR_CLIENT_NOT_CONNECTED);
				}
			}
			else {
				//Probably the client is looking for an old host_room that
				//was deleted and does not exist anymore, notify the client
				SendMsg(other_socket, MESSAGE_ERROR_ROOM_NO_LONGER_EXISTS);
			}


		}break;
		case MESSAGE_REQUEST_SEND_COMPLETE_CAPTURE: {
			if (!is_this_client_hosting)
				break;

			s32 room_index = -1;
			for (u32 i = 0; i < rooms.size(); i++) {
				if (rooms[i].host_socket == other_socket)
					room_index = i;
			}
			ASSERT(room_index != -1);

			//TODO this is temporary, should probably care more about the memory
			u32 compressed_size;
			Receive(other_socket, &compressed_size);
			std::vector<u8> buffer(compressed_size);
			ReceiveBuffer(other_socket, buffer.data(), buffer.size());
			for (u32 i = 0; i < XUSER_MAX_COUNT; i++) {
				auto& connected_socket = rooms[room_index].connected_sockets[i];
				if (connected_socket.connected) {
					SendMsg(connected_socket.sock, MESSAGE_REQUEST_SEND_COMPLETE_CAPTURE);
					Send(connected_socket.sock, compressed_size);
					SendBuffer(connected_socket.sock, buffer.data(), buffer.size());
				}
			}
		}break;
		case MESSAGE_REQUEST_SEND_PARTIAL_CAPTURE: {
			if (!is_this_client_hosting)
				break;

			s32 room_index = -1;
			for (u32 i = 0; i < rooms.size(); i++) {
				if (rooms[i].host_socket == other_socket)
					room_index = i;
			}
			ASSERT(room_index != -1);
			u32 changed_regions;

			Receive(other_socket, &changed_regions);
			//TODO fix this, extremely slow
			PartialCapture* captures = new PartialCapture[changed_regions];
			ReceiveBuffer(other_socket, captures, changed_regions * sizeof(PartialCapture));
			u32 partial_buffer_size = 0;

			for (u32 i = 0; i < changed_regions; i++)
				partial_buffer_size += captures[i].end_index - captures[i].begin_index;

			std::vector<u8> buffer(partial_buffer_size);
			u32 buffer_offset = 0;
			for (u32 i = 0; i < changed_regions; i++) {
				PartialCapture& capture = captures[i];
				ReceiveBuffer(other_socket, buffer.data() + buffer_offset, capture.end_index - capture.begin_index);
				buffer_offset += capture.end_index - capture.begin_index;
			}

			for (u32 i = 0; i < XUSER_MAX_COUNT; i++) {
				auto& connected_socket = rooms[room_index].connected_sockets[i];
				if (connected_socket.connected) {
					SendMsg(connected_socket.sock, MESSAGE_REQUEST_SEND_PARTIAL_CAPTURE);
					Send(connected_socket.sock, changed_regions);
					SendBuffer(connected_socket.sock, captures, changed_regions * sizeof(PartialCapture));
					SendBuffer(connected_socket.sock, buffer.data(), buffer.size());
				}
			}

			delete[] captures;

		}break;
		case MESSAGE_INFO_CHANGED_CAPTURED_SCREEN_DIMENSIONS: {
			if (!is_this_client_hosting)
				break;

			s32 room_index = -1;
			for (u32 i = 0; i < rooms.size(); i++) {
				if (rooms[i].host_socket == other_socket)
					room_index = i;
			}
			ASSERT(room_index != -1);

			s32 new_windows_width, new_windows_height;
			Receive(other_socket, &new_windows_width);
			Receive(other_socket, &new_windows_height);

			for (u32 i = 0; i < XUSER_MAX_COUNT; i++) {
				auto& connected_socket = rooms[room_index].connected_sockets[i];
				if (connected_socket.connected) {
					SendMsg(connected_socket.sock, MESSAGE_INFO_CHANGED_CAPTURED_SCREEN_DIMENSIONS);
					Send(connected_socket.sock, new_windows_width);
					Send(connected_socket.sock, new_windows_height);
				}
			}
		}
		case MESSAGE_ERROR_HOST_COULD_NOT_ALLOCATE_PAD:
		case MESSAGE_INFO_PAD_ALLOCATED: {
			if (is_this_client_hosting) {
				u32 room_index = 0;
				for (u32 i = 0; i < rooms.size(); i++) {
					if (rooms[i].host_socket == other_socket) {
						room_index = i;
						break;
					}
				}

				Room::SyncPrimitives* sync_primitives = LockSyncPrimitive(server_data, rooms[room_index].sync_primitives_index);
				std::unique_lock lk{ sync_primitives->mtx };
				rooms[room_index].connecting_message = msg == MESSAGE_ERROR_HOST_COULD_NOT_ALLOCATE_PAD ? msg : MESSAGE_ERROR_NONE;
				sync_primitives->notify_cv.notify_one();
				UnlockSyncPrimitive(server_data, rooms[room_index].sync_primitives_index);
			}
		}break;

		case MESSAGE_INFO_ROOM_CLOSING: {
			// TODO, we could notify the room members right away, atm they get notified right after 
			// having sent more pad data
			for (u32 i = 0; i < rooms.size(); i++) {
				if (rooms[i].host_socket == other_socket) {

					for (u32 j = 0; j < XUSER_MAX_COUNT; j++) {
						auto& sk = rooms[i].connected_sockets[j];
						if(sk.connected)
							SendMsg(sk.sock, MESSAGE_ERROR_ROOM_NO_LONGER_EXISTS);
					}

					rooms.erase(rooms.begin() + i);
					break;
				}
			}
		}break;
		}
	}
}
