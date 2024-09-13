#pragma once
#include "incl.hpp"
#include <list>

namespace Audio
{
    const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
    const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
    const IID IID_IAudioClient = __uuidof(IAudioClient);
    const IID IID_IAudioCaptureClient = __uuidof(IAudioCaptureClient);
    const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);

    static constexpr u32 sample_rate = 48000;
    static constexpr f32 frequency = 440.f;
    static constexpr u32 buffer_length_in_seconds = 1;
    static constexpr u32 unit_packet_size_in_frames = 480;
    static constexpr u32 frame_size_in_bytes = 8;
    static constexpr u32 unit_packet_size_in_bytes = unit_packet_size_in_frames * frame_size_in_bytes;
    static constexpr s32 silent_noise_threshold = 20;

    struct Payload
    {
        u8 data[unit_packet_size_in_bytes];
        bool initialized = false;
    };

    struct Device
    {
        HRESULT hr;
        IMMDeviceEnumerator* device_enumerator = nullptr;
        IMMDevice* device = nullptr;
        IAudioClient* audio_client = nullptr;
        IAudioCaptureClient* capture_client = nullptr;
        IAudioRenderClient* render_client = nullptr;
        WAVEFORMATEX wfx;
        HANDLE event_handle;
        u32 buffer_frame_count;

        void Release() {
            if (event_handle != NULL) CloseHandle(event_handle); 
            if (render_client) render_client->Release();
            if (capture_client) capture_client->Release();
            if (audio_client) audio_client->Release();
            if (device) device->Release();
            if (device_enumerator) device_enumerator->Release();
            CoUninitialize();
        }
    };

    HRESULT InitDevice(Device* device, bool is_capture_client);
    void DestroyDevice(Device& devices);
	s32 CaptureSystemAudio();
	u32 FillAudioBuffer();

    HRESULT CaptureAudioFrame(Device& dev, Payload& first_frame, Payload& second_frame);
    HRESULT RenderAudioFrame(Device& dev, Payload& payload);
}