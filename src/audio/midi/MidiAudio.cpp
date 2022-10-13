// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "precomp.h"
#include "MidiAudio.hpp"
#include "../terminal/parser/stateMachine.hpp"

#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

using Microsoft::WRL::ComPtr;
using namespace std::chrono_literals;

// The WAVE_DATA below is an 8-bit PCM encoding of a triangle wave form.
// We just play this on repeat at varying frequencies to produce our notes.
constexpr auto WAVE_SIZE = 16u;
constexpr auto WAVE_DATA = std::array<byte, WAVE_SIZE>{ 128, 159, 191, 223, 255, 223, 191, 159, 128, 96, 64, 32, 0, 32, 64, 96 };

MidiAudio::MidiAudio(HWND)
{
    const auto enumerator = wil::CoCreateInstance<MMDeviceEnumerator, IMMDeviceEnumerator>();
    THROW_IF_FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, _device.put()));
    THROW_IF_FAILED(_device->Activate(__uuidof(_client), CLSCTX_ALL, NULL, _client.put_void()));

    wil::unique_any<WAVEFORMATEX*, decltype(&::CoTaskMemFree), ::CoTaskMemFree> format;
    THROW_IF_FAILED(_client->GetMixFormat(format.addressof()));
    THROW_IF_FAILED(_client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0, format.get(), NULL));

    _event.create();
    THROW_IF_FAILED(_client->SetEventHandle(_event.get()));
}

MidiAudio::~MidiAudio() noexcept
{
    try
    {
#pragma warning(suppress : 26447)
        // We acquire the lock here so the class isn't destroyed while in use.
        // If this throws, we'll catch it, so the C26447 warning is bogus.
        const auto lock = std::unique_lock{ _inUseMutex };
    }
    catch (...)
    {
        // If the lock fails, we'll just have to live with the consequences.
    }
}

void MidiAudio::Initialize()
{
    _shutdownFuture = _shutdownPromise.get_future();
}

void MidiAudio::Shutdown()
{
    // Once the shutdown promise is set, any note that is playing will stop
    // immediately, and the Unlock call will exit the thread ASAP.
    _shutdownPromise.set_value();
}

void MidiAudio::Lock()
{
    _inUseMutex.lock();
}

void MidiAudio::Unlock()
{
    // We need to check the shutdown status before releasing the mutex,
    // because after that the class could be destroyed.
    const auto shutdownStatus = _shutdownFuture.wait_for(0s);
    _inUseMutex.unlock();
    // If the wait didn't timeout, that means the shutdown promise was set,
    // so we need to exit the thread ASAP by throwing an exception.
    if (shutdownStatus != std::future_status::timeout)
    {
        throw Microsoft::Console::VirtualTerminal::StateMachine::ShutdownException{};
    }
}

void MidiAudio::PlayNote(const int noteNumber, const int velocity, const std::chrono::microseconds duration) noexcept
try
{
    if (velocity)
    {
        // The formula for frequency is 2^(n/12) * 440Hz, where n is zero for
        // the A above middle C (A4). In MIDI terms, A4 is note number 69,
        // which is why we subtract 69. We also need to multiply by the size
        // of the wave form to determine the frequency that the sound buffer
        // has to be played to achieve the equivalent note frequency.
        const auto frequency = std::pow(2.0, (noteNumber - 69.0) / 12.0) * 440.0 * WAVE_SIZE;
        // For the volume, we're using the formula defined in the General
        // MIDI Level 2 specification: Gain in dB = 40 * log10(v/127). We need
        // to multiply by 4000, though, because the SetVolume method expects
        // the volume to be in hundredths of a decibel.
        const auto volume = 4000.0 * std::log10(velocity / 127.0);
        
        buffer->SetFrequency(gsl::narrow_cast<DWORD>(frequency));
        buffer->SetVolume(gsl::narrow_cast<LONG>(volume));
        // Resetting the buffer to a position that is slightly off from the
        // last position will help to produce a clearer separation between
        // tones when repeating sequences of the same note.
        buffer->SetCurrentPosition((_lastBufferPosition + 12) % WAVE_SIZE);
    }

    // By waiting on the shutdown future with the duration of the note, we'll
    // either be paused for the appropriate amount of time, or we'll break out
    // of the wait early if we've been shutdown.
    _shutdownFuture.wait_for(duration);

    if (velocity)
    {
        // When the note ends, we just turn the volume down instead of stopping
        // the sound buffer. This helps reduce unwanted static between notes.
        buffer->SetVolume(DSBVOLUME_MIN);
        buffer->GetCurrentPosition(&_lastBufferPosition, nullptr);
    }
}
CATCH_LOG()
