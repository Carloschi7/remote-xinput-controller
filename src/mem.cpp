#include "mem.hpp"

namespace Core
{
	FixedBuffer::FixedBuffer(FixedBufferType type) : type{ type }
	{
		switch (type)
		{
		case FIXED_BUFFER_TYPE_CLIENT: 
			for (u32 i = 0; i < CLIENT_ALLOCATIONS_SIZE; i++) {
				buf_size += g_client_allocations_offsets[i];
			}
			break;
		case FIXED_BUFFER_TYPE_HOST:
			for (u32 i = 0; i < HOST_ALLOCATIONS_SIZE; i++) {
				buf_size += g_host_allocations_offsets[i];
			}
		}

		buf = ::operator new(buf_size);
		//If this does not succeed, the program cannot start(not enough mem can be allocated)
		ASSERT(buf);
	}

	FixedBuffer::~FixedBuffer() noexcept
	{
		::operator delete(buf);
	}

	void FixedBuffer::ResetMemory()
	{
		ZeroMemory(buf, buf_size);
	}

	void* FixedBuffer::GetClientSection(ClientAllocations allocation)
	{
		ASSERT(type == FIXED_BUFFER_TYPE_CLIENT && allocation < CLIENT_ALLOCATIONS_SIZE);
		u32 offset = 0;
		for (u32 i = 0; i < allocation; i++) {
			offset += g_client_allocations_offsets[i];
		}

		return static_cast<u8*>(buf) + offset;
	}
	void* FixedBuffer::GetHostSection(HostAllocations allocation)
	{
		ASSERT(type == FIXED_BUFFER_TYPE_HOST && allocation < HOST_ALLOCATIONS_SIZE);
		u32 offset = 0;
		for (u32 i = 0; i < allocation; i++) {
			offset += g_host_allocations_offsets[i];
		}

		return static_cast<u8*>(buf) + offset;
	}
}


