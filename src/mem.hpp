#pragma once
#include "incl.hpp"

namespace Core
{
	class FixedBuffer
	{
	public:
		FixedBuffer();
		FixedBuffer(FixedBufferType type);
		FixedBuffer(const FixedBuffer&) = delete;
		~FixedBuffer() noexcept;

		void Init(FixedBufferType type);
		inline bool Initialized() { return initialized; }

		void ResetState();
		void ResetMemory();
		void* GetClientSection(ClientAllocations allocation);
		void* GetHostSection(HostAllocations allocation);
		void* GetWxSection(WxAllocations allocation);

	private:
		void* buf = nullptr;
		u32 buf_size = 0;
		FixedBufferType type;
		bool initialized = false;
	};
}

