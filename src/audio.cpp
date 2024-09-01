#include "audio.hpp"
#include <iostream>
#include "incl.hpp"


#define REFTIMES_PER_SEC       10000000
#define REFTIMES_PER_MILLISEC  10000

#define XE_AUDIO_ASSERT(inst)\
    dev.hr = inst;\
    if (FAILED(dev.hr)) {\
        dev.Release();\
        return dev.hr;\
    }

namespace Audio
{

    const UINT32 SAMPLE_RATE = 48000; // Sample rate in Hz
    const f32 FREQUENCY = 440.f;     // Frequency of the sine wave in Hz
    const UINT32 DURATION_MS = 4000;  // Duration of playback in milliseconds
    const UINT32 BUFFER_FRAME_COUNT = SAMPLE_RATE * DURATION_MS / 1000; // Number of frames to play
    const u32 buffer_length_in_seconds = 1;

    s32 CaptureSystemAudio()
    {
        Devices dev = {};

        // Initialize COM
        XE_AUDIO_ASSERT(CoInitialize(NULL));
        XE_AUDIO_ASSERT(CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
            IID_IMMDeviceEnumerator, (void**)&dev.device_enumerator));
        XE_AUDIO_ASSERT(dev.device_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &dev.device));
        XE_AUDIO_ASSERT(dev.device->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&dev.audio_client));

        // Initialize the audio client with the desired format
        dev.wfx.wFormatTag = WAVE_FORMAT_PCM;
        dev.wfx.nChannels = 2; // Stereo
        dev.wfx.nSamplesPerSec = SAMPLE_RATE;
        dev.wfx.wBitsPerSample = 32;
        dev.wfx.nBlockAlign = (dev.wfx.nChannels * dev.wfx.wBitsPerSample) / 8;
        dev.wfx.nAvgBytesPerSec = dev.wfx.nSamplesPerSec * dev.wfx.nBlockAlign;
        dev.wfx.cbSize = 0;

        XE_AUDIO_ASSERT(dev.audio_client->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK,
            REFTIMES_PER_SEC * buffer_length_in_seconds,
            0,
            &dev.wfx,
            NULL));

        // Get the size of the allocated buffer
        u32 buffer_frame_count;
        u32 packet_length = 0;

        XE_AUDIO_ASSERT(dev.audio_client->GetBufferSize(&buffer_frame_count));
        XE_AUDIO_ASSERT(dev.audio_client->GetService(IID_IAudioCaptureClient,(void**)&dev.capture_client));
        XE_AUDIO_ASSERT(dev.audio_client->Start());

        // Capture loop
        for (;;) {
            // Sleep for half the buffer duration
            Sleep((DWORD)(buffer_length_in_seconds * 1000 / 2));

            // Get the available data in the shared buffer
            XE_AUDIO_ASSERT(dev.capture_client->GetNextPacketSize(&packet_length));
            while (packet_length != 0) {
                // Get the captured data
                u8* pData;
                DWORD flags;

                XE_AUDIO_ASSERT(dev.capture_client->GetBuffer(&pData,&packet_length,&flags, NULL, NULL));
                // If the flag is silent, fill with silence
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    pData = NULL;  // Treat as silence
                }

                XE_AUDIO_ASSERT(dev.capture_client->ReleaseBuffer(packet_length));
                XE_AUDIO_ASSERT(dev.capture_client->GetNextPacketSize(&packet_length));
            }
        }

        // Stop capturing
        XE_AUDIO_ASSERT(dev.audio_client->Stop());
        // Cleanup
        dev.Release();
        return dev.hr;
    }



    u32 FillAudioBuffer()
    {
        Devices dev = {};
        u8* pData = nullptr;
        u8* source_audio = nullptr;

        // Initialize COM
        XE_AUDIO_ASSERT(CoInitialize(NULL));
        XE_AUDIO_ASSERT(CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
            IID_IMMDeviceEnumerator, (void**)&dev.device_enumerator));
        XE_AUDIO_ASSERT(dev.device_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &dev.device));
        XE_AUDIO_ASSERT(dev.device->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&dev.audio_client));

        // Initialize the audio client with the desired format
        dev.wfx.wFormatTag = WAVE_FORMAT_PCM;
        dev.wfx.nChannels = 2; // Stereo
        dev.wfx.nSamplesPerSec = SAMPLE_RATE;
        dev.wfx.wBitsPerSample = 32;
        dev.wfx.nBlockAlign = (dev.wfx.nChannels * dev.wfx.wBitsPerSample) / 8;
        dev.wfx.nAvgBytesPerSec = dev.wfx.nSamplesPerSec * dev.wfx.nBlockAlign;
        dev.wfx.cbSize = 0;

        XE_AUDIO_ASSERT(dev.audio_client->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            REFTIMES_PER_SEC * buffer_length_in_seconds,
            0,
            &dev.wfx,
            NULL));

        dev.event_handle = CreateEvent(0, FALSE, FALSE, 0);
        if (dev.event_handle == NULL) {
            Log::Format("[ERROR in {}]: Could not create wait event\n", __FUNCTION__);
            return -1;
        }

        XE_AUDIO_ASSERT(dev.audio_client->SetEventHandle(dev.event_handle));
        XE_AUDIO_ASSERT(dev.audio_client->GetService(IID_IAudioRenderClient, (void**)&dev.render_client));

        // Allocate memory for the buffer
        u32 bufferFrameCount;
        XE_AUDIO_ASSERT(dev.audio_client->GetBufferSize(&bufferFrameCount));

        u32 bufferSize = bufferFrameCount * dev.wfx.nBlockAlign;
        source_audio = new u8[bufferSize];
        s32* source_audio_u32 = reinterpret_cast<s32*>(source_audio);

        // Generate the sine wave data
        u64 gen = 0;
        XE_AUDIO_ASSERT(dev.audio_client->Start());

        // Play for the specified duration
        for (;;) {
            WaitForSingleObject(dev.event_handle, INFINITE);

            u32 padding = 0;
            XE_AUDIO_ASSERT(dev.audio_client->GetCurrentPadding(&padding));
            u32 count = bufferFrameCount - padding;

            if (count != 0) {
                for (u32 i = 0; i < count; ++i) {
                    f32 pi = 3.14159265359f;
                    f32 theta = 2.0f * pi * FREQUENCY * gen++ / SAMPLE_RATE;
                    source_audio_u32[i * 2] = static_cast<s32>(0.5f * std::sin(theta) * 0x7FFFFFFF); // Left channel
                    source_audio_u32[i * 2 + 1] = static_cast<s32>(0.5f * std::sin(theta) * 0x7FFFFFFF); // Right channel
                }

                BYTE* pData;
                XE_AUDIO_ASSERT(dev.render_client->GetBuffer(count, &pData));
                std::memcpy(pData, source_audio_u32, count * dev.wfx.nBlockAlign);
                XE_AUDIO_ASSERT(dev.render_client->ReleaseBuffer(count, 0));
            }
        }

        XE_AUDIO_ASSERT(dev.audio_client->Stop()); // Stop playback
        dev.Release();
        return 0;
    }
}

