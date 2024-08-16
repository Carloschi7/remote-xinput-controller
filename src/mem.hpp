#pragma once
#include "incl.hpp"

namespace Core
{
	class FixedBuffer
	{
	public:
		FixedBuffer(FixedBufferType type);
		FixedBuffer(const FixedBuffer&) = delete;
		~FixedBuffer() noexcept;

		void ResetMemory();
		void* GetClientSection(ClientAllocations allocation);
		void* GetHostSection(HostAllocations allocation);

	private:
		void* buf = nullptr;
		u32 buf_size = 0;
		FixedBufferType type;
	};
}

