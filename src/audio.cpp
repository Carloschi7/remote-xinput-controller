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
    const u32 SAMPLE_RATE = 48000; // Sample rate in Hz
    const f32 FREQUENCY = 440.f;     // Frequency of the sine wave in Hz
    const u32 DURATION_MS = 4000;  // Duration of playback in milliseconds
    const u32 BUFFER_FRAME_COUNT = SAMPLE_RATE * DURATION_MS / 1000; // Number of frames to play
    const u32 buffer_length_in_seconds = 1;

    HRESULT InitDevice(Device* device, bool is_capture_client)
    {
        Device dev = {};
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

        WAVEFORMATEX* closest;
        HRESULT hr = dev.audio_client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &dev.wfx, &closest);
        if (hr != S_OK) {
            Log::Format("The audio wave type is not supported\n");
            return hr;
        }
        
        u32 stream_flag = is_capture_client ? 
            AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK :
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK;

        XE_AUDIO_ASSERT(dev.audio_client->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            stream_flag,
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

        if (is_capture_client) {
            XE_AUDIO_ASSERT(dev.audio_client->GetService(IID_IAudioCaptureClient, (void**)&dev.capture_client));
        }
        else {
            XE_AUDIO_ASSERT(dev.audio_client->GetService(IID_IAudioRenderClient, (void**)&dev.render_client));
        }

        // Allocate memory for the buffer
        XE_AUDIO_ASSERT(dev.audio_client->GetBufferSize(&dev.buffer_frame_count));
        XE_AUDIO_ASSERT(dev.audio_client->Start());

        std::memcpy(device, &dev, sizeof(Device));
    }

    void DestroyDevice(Device& devices)
    {
        devices.Release();
    }

    s32 CaptureSystemAudio()
    {
        Device dev = {};
        InitDevice(&dev, true);
        

        // Capture loop
        for (;;) {
            u32 packet_length;
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
        Device dev = {};
        InitDevice(&dev, false);

        u32 bufferSize = dev.buffer_frame_count * dev.wfx.nBlockAlign;
        u8* source_audio = new u8[bufferSize];
        s32* source_audio_u32 = reinterpret_cast<s32*>(source_audio);

        u64 gen = 0;
        // Play for the specified duration
        for (;;) {
            WaitForSingleObject(dev.event_handle, INFINITE);

            u32 padding = 0;
            XE_AUDIO_ASSERT(dev.audio_client->GetCurrentPadding(&padding));
            u32 count = dev.buffer_frame_count - padding;

            if (count != 0) {
                for (u32 i = 0; i < count; ++i) {
                    f32 pi = 3.14159265359f;
                    f32 theta = 2.0f * pi * FREQUENCY * gen++ / SAMPLE_RATE;
                    s32 value = static_cast<s32>(0.5f * std::sin(theta) * 0x7FFFFFFF);
                    source_audio_u32[i * 2] = value; // Left channel
                    source_audio_u32[i * 2 + 1] = value; // Right channel
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

    HRESULT CaptureAudioFrame(Device& dev, Payload& first_frame, Payload& second_frame)
    {
        // Get the captured data
        DWORD flags;
        u8* data;
        u32 pack_length;

        XE_AUDIO_ASSERT(dev.capture_client->GetNextPacketSize(&pack_length));
        XE_AUDIO_ASSERT(dev.capture_client->GetBuffer(&data, &pack_length, &flags, NULL, NULL));
        XE_ASSERT(pack_length == 0 || pack_length == 480 ||
            pack_length == 960, "Need a better system to handle frames\n");
        // If the flag is silent, fill with silence
        first_frame.initialized = false;
        second_frame.initialized = false;
        if (flags == 0 && pack_length != 0) {
            u32 size = pack_length * dev.wfx.nBlockAlign;
            s32* buffer_s32 = reinterpret_cast<s32*>(data);
            //If we only have low background system noise, we dont send that
            if (buffer_s32[size / 4 - 1] > 20 || buffer_s32[size / 4 - 1] < -20) {
                if (pack_length == 960) {
                    //Double frame
                    std::memcpy(first_frame.data, data, 480 * 8);
                    std::memcpy(second_frame.data, data + 480 * 8, 480 * 8);
                    second_frame.initialized = true;
                }
                else {
                    std::memcpy(first_frame.data, data, size);
                }
                first_frame.initialized = true;
            }
        }
        XE_AUDIO_ASSERT(dev.capture_client->ReleaseBuffer(pack_length));
    }

    HRESULT RenderAudioFrame(Device& dev, Payload& payload)
    {
        static u64 gen = 0;
        static u32 source_audio_u32[48000 * 2];
        if (gen == 0) {
            for (u32 i = 0; i < 48000; ++i) {
                f32 pi = 3.14159265359f;
                f32 theta = 2.0f * pi * FREQUENCY * gen++ / SAMPLE_RATE;
                s32 value = static_cast<s32>(0.5f * std::sin(theta) * 0x7FFFFFFF);
                source_audio_u32[i * 2] = value; // Left channel
                source_audio_u32[i * 2 + 1] = value; // Right channel
            }
        }

        //Sleep(100);
        WaitForSingleObject(dev.event_handle, INFINITE);

        u32 padding = 0;
        XE_AUDIO_ASSERT(dev.audio_client->GetCurrentPadding(&padding));
        //Atm, the default packet size we fill each frame is 1/100 of the buffer size
        //TODO find a better way of filling it
        u32 count = std::min((DWORD)480 - padding, dev.wfx.nSamplesPerSec / 100);

        //static u64 cur = 0;
        //std::memcpy(payload.data, ((u8*)source_audio_u32) + cur, 3840);
        //cur = (cur + 3840) % (48000*8);

        if (count != 0) {
            BYTE* pData;
            XE_AUDIO_ASSERT(dev.render_client->GetBuffer(count, &pData));
            u32 byte_count = count * dev.wfx.nBlockAlign;
            std::memcpy(pData, payload.data, byte_count);
            XE_AUDIO_ASSERT(dev.render_client->ReleaseBuffer(count, 0));
        }

        //if (count != 0) {
        //    BYTE* pData;
        //    XE_AUDIO_ASSERT(dev.render_client->GetBuffer(count, &pData));
        //    u32 byte_count = 3840;
        //    std::memcpy(pData, payload.data, byte_count);
        //    XE_AUDIO_ASSERT(dev.render_client->ReleaseBuffer(count, 0));
        //}

        return 0;
    }

    HRESULT RenderAudioFramev()
    {
        Audio::Device dev;
        Audio::InitDevice(&dev, false);

        static u64 gen = 0;
        static u32 source_audio_u32[48000 * 2];
        if (gen == 0) {
            for (u32 i = 0; i < 48000; ++i) {
                f32 pi = 3.14159265359f;
                f32 theta = 2.0f * pi * FREQUENCY * gen++ / SAMPLE_RATE;
                s32 value = static_cast<s32>(0.5f * std::sin(theta) * 0x7FFFFFFF);
                source_audio_u32[i * 2] = value; // Left channel
                source_audio_u32[i * 2 + 1] = value; // Right channel
            }
        }

        //XE_ASSERT(play_cursor, "the pointer must be defined\n");

        u32 cursor = 0;
        u32 count = 0;
        while (true) {
            WaitForSingleObject(dev.event_handle, INFINITE);
            u32 padding;
            XE_AUDIO_ASSERT(dev.audio_client->GetCurrentPadding(&padding));
            //Atm, the default packet size we fill each frame is 1/100 of the buffer size
            //TODO find a better way of filling it
            count = std::min((DWORD)480 - padding, dev.wfx.nSamplesPerSec / 100);

            if (count != 0) {
                BYTE* pData;
                XE_AUDIO_ASSERT(dev.render_client->GetBuffer(count, &pData));
                u32 byte_count = count * dev.wfx.nBlockAlign;
                std::memcpy(pData, ((u8*)source_audio_u32) + cursor, byte_count);
                cursor = (cursor + byte_count) % (dev.wfx.nSamplesPerSec * dev.wfx.nBlockAlign);
                //remaining_bytes -= byte_count;
                XE_AUDIO_ASSERT(dev.render_client->ReleaseBuffer(count, 0));
            }
        }

        return 0;
    }
}

