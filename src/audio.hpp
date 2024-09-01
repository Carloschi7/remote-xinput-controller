#pragma once
#include "incl.hpp"

namespace Audio
{
    const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
    const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
    const IID IID_IAudioClient = __uuidof(IAudioClient);
    const IID IID_IAudioCaptureClient = __uuidof(IAudioCaptureClient);
    const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);

    struct Devices
    {
        HRESULT hr;
        IMMDeviceEnumerator* device_enumerator = nullptr;
        IMMDevice* device = nullptr;
        IAudioClient* audio_client = nullptr;
        IAudioCaptureClient* capture_client = nullptr;
        IAudioRenderClient* render_client = nullptr;
        WAVEFORMATEX wfx;
        HANDLE event_handle;

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

	s32 CaptureSystemAudio();
	u32 FillAudioBuffer();
}