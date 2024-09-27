#include "mem.hpp"

namespace Core
{
	FixedBuffer::FixedBuffer()
	{
		initialized = false;
	}

	FixedBuffer::FixedBuffer(FixedBufferType type)
	{
		Init(type);
	}

	FixedBuffer::~FixedBuffer() noexcept
	{
		::operator delete(buf);
	}

	void FixedBuffer::Init(FixedBufferType type)
	{
		this->type = type;

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
		case FIXED_BUFFER_TYPE_WX:
			for (u32 i = 0; i < WX_ALLOCATIONS_SIZE; i++) {
				buf_size += g_wx_allocations_offsets[i];
			}
		}

		buf = ::operator new(buf_size);
		//If this does not succeed, the program cannot start(not enough mem can be allocated)
		XE_ASSERT(buf, "allocation failed\n");
		initialized = true;
	}

	void FixedBuffer::ResetState()
	{
		::operator delete(buf);
		buf_size = 0;
		initialized = false;
	}

	void FixedBuffer::ResetMemory()
	{
		XE_ASSERT(initialized, "FixedBuffer needs to be initialized\n");
		ZeroMemory(buf, buf_size);
	}

	void* FixedBuffer::GetClientSection(ClientAllocations allocation)
	{
		XE_ASSERT(initialized && type == FIXED_BUFFER_TYPE_CLIENT && allocation < CLIENT_ALLOCATIONS_SIZE, 
			"Wrong parameter or wrong fixed buffer type selection\n");

		u32 offset = 0;
		for (u32 i = 0; i < allocation; i++) {
			offset += g_client_allocations_offsets[i];
		}

		return static_cast<u8*>(buf) + offset;
	}
	void* FixedBuffer::GetHostSection(HostAllocations allocation)
	{
		XE_ASSERT(initialized && type == FIXED_BUFFER_TYPE_HOST && allocation < HOST_ALLOCATIONS_SIZE,
			"Wrong parameter or wrong fixed buffer type selection\n");

		u32 offset = 0;
		for (u32 i = 0; i < allocation; i++) {
			offset += g_host_allocations_offsets[i];
		}

		return static_cast<u8*>(buf) + offset;
	}
	void* FixedBuffer::GetWxSection(WxAllocations allocation)
	{
		XE_ASSERT(initialized && type == FIXED_BUFFER_TYPE_WX && allocation < WX_ALLOCATIONS_SIZE,
			"Wrong parameter or wrong fixed buffer type selection\n");

		u32 offset = 0;
		for (u32 i = 0; i < allocation; i++) {
			offset += g_wx_allocations_offsets[i];
		}

		return static_cast<u8*>(buf) + offset;

		return nullptr;
	}
}


