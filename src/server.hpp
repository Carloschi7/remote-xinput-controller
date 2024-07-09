#pragma once
#include "incl.hpp"
#include <vector>

struct SyncPrimitiveHeap
{
	bool slot_taken = false;
	Room::SyncPrimitives data;
};

struct ServerData
{
	std::atomic<u64> id_generator;
	std::mutex rooms_mutex;
	std::vector<Room> rooms;
	//Mutexes and condition variables are very obnoxious to put in a structs as
	//direct objects, for the move & copy restrictions that come with them, creating a
	//little virtual heap for them is the best solution. This pointer holds the data of an
	//array of a bunch of SyncPrimitives
	SyncPrimitiveHeap* sync_primitive_heap_ptr = nullptr;
	u32 sync_primitive_heap_count;
	u32 borrows = 0;
	std::mutex heap_mtx;

	~ServerData() {
		if (sync_primitive_heap_ptr) {
			delete[] sync_primitive_heap_ptr;
			sync_primitive_heap_ptr = nullptr;
		}
	}
};



u32 AllocateNewSyncPrimitive(ServerData* server_data);
Room::SyncPrimitives* LockSyncPrimitive(ServerData* server_data, u32 index);
void UnlockSyncPrimitive(ServerData* server_data, u32 index);
void FreeSyncPrimitive(ServerData* server_data, u32 index);

SOCKET SetupServerSocket(USHORT port);
void StartServer();
void PingRooms(ServerData* server_data);
void HandleConnection(ServerData* server_data, SOCKET other_socket);