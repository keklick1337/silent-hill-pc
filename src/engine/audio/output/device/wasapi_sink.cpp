// SPDX-License-Identifier: GPL-3.0-or-later
// The mix through WASAPI, Windows' own audio interface, on the system's default playback device. The device is
// opened event-driven: Windows signals an event whenever it wants more sound, and a thread of this sink answers.
//
// Shared with other programs, the device runs in the format of Windows' mixer, and the sink only accepts it when
// that is the mix's own (two channels of integer PCM at the asked rate) — which Windows' mixer, working in
// floating point, practically never offers; OutputDevice then falls back to SDL. Exclusive, the device takes the
// mix's samples directly, at the mix rate or with each frame repeated at a multiple of it.

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <initguid.h>
#include <audioclient.h>
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>

#include "engine/audio/output/device/platform_sinks.h"
#include "engine/audio/output/mix_feed.h"

#include <algorithm>
#include <future>
#include <optional>
#include <string>
#include <system_error>
#include <thread>

namespace sh::audio::output {

namespace {

constexpr DWORD kStreamFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST;
// AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM and _SRC_DEFAULT_QUALITY: Windows' mixer converts the stream to its format.
constexpr DWORD kConvertFlags = 0x80000000u | 0x08000000u;
constexpr REFERENCE_TIME kTicksPerMs = 10'000;  // REFERENCE_TIME counts 100 ns
constexpr std::uint64_t kTicksPerSecond = 10'000'000;
// The mix is rendered at least this far ahead of the device's play position.
constexpr std::uint32_t kLeastLeadFrames = 256;

// A COM interface pointer released when it goes out of scope.
template <typename Interface>
class ComRef {
public:
    ComRef() = default;
    ~ComRef() { drop(); }
    ComRef(const ComRef&) = delete;
    ComRef& operator=(const ComRef&) = delete;

    void drop() {
        if (pointer_ != nullptr) pointer_->Release();
        pointer_ = nullptr;
    }
    // Where a COM call stores a new interface; what was held is released first.
    Interface** slot() {
        drop();
        return &pointer_;
    }
    void** untyped_slot() { return reinterpret_cast<void**>(slot()); }
    Interface* get() const { return pointer_; }
    Interface* operator->() const { return pointer_; }
    explicit operator bool() const { return pointer_ != nullptr; }

private:
    Interface* pointer_ = nullptr;
};

// COM on the calling thread for as long as the object lives.
class ComSession {
public:
    ComSession() : entered_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComSession() {
        if (SUCCEEDED(entered_)) CoUninitialize();
    }
    // A thread that already runs COM in the other threading model can still use the audio interfaces.
    bool usable() const { return SUCCEEDED(entered_) || entered_ == RPC_E_CHANGED_MODE; }
    HRESULT code() const { return entered_; }

private:
    HRESULT entered_;
};

// Windows' scheduling class for audio threads, for as long as the object lives. Without it the thread still
// plays, only more exposed to the load of the machine.
class AudioThreadPriority {
public:
    AudioThreadPriority() {
        DWORD task_index = 0;
        task_ = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
    }
    ~AudioThreadPriority() {
        if (task_ != nullptr) AvRevertMmThreadCharacteristics(task_);
    }

private:
    HANDLE task_ = nullptr;
};

struct TaskMemoryFree {
    void operator()(void* memory) const { CoTaskMemFree(memory); }
};

struct Failure {
    OutputResult result = OutputResult::SinkError;
    std::int64_t native = 0;
    const char* note = "";
};

OutputResult result_of(HRESULT code) {
    switch (code) {
        case AUDCLNT_E_DEVICE_IN_USE: return OutputResult::DeviceBusy;
        case AUDCLNT_E_UNSUPPORTED_FORMAT: return OutputResult::UnsupportedFormat;
        case AUDCLNT_E_DEVICE_INVALIDATED:
        case AUDCLNT_E_RESOURCES_INVALIDATED: return OutputResult::DeviceLost;
        default: return OutputResult::SinkError;
    }
}

Failure failure_of(HRESULT code, const char* note) { return Failure{result_of(code), code, note}; }

std::string utf8_of(const wchar_t* wide) {
    if (wide == nullptr || *wide == L'\0') return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) return {};
    std::string text(static_cast<std::size_t>(bytes - 1), '\0');
    // std::string keeps room for the terminator the call writes.
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, text.data(), bytes, nullptr, nullptr);
    return text;
}

std::string friendly_name(IMMDevice* endpoint) {
    ComRef<IPropertyStore> properties;
    if (FAILED(endpoint->OpenPropertyStore(STGM_READ, properties.slot()))) return {};
    PROPVARIANT value;
    PropVariantInit(&value);
    std::string name;
    if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR) {
        name = utf8_of(value.pwszVal);
    }
    PropVariantClear(&value);
    return name;
}

// The stereo stream of `format` at `rate`, as Windows describes formats.
WAVEFORMATEXTENSIBLE describe(SampleFormat format, std::uint32_t rate, bool plain_header) {
    WAVEFORMATEXTENSIBLE wave{};
    wave.Format.wFormatTag = plain_header ? WAVE_FORMAT_PCM : WAVE_FORMAT_EXTENSIBLE;
    wave.Format.nChannels = 2;
    wave.Format.nSamplesPerSec = rate;
    wave.Format.wBitsPerSample = container_bits(format);
    wave.Format.nBlockAlign = static_cast<WORD>(frame_bytes(format));
    wave.Format.nAvgBytesPerSec = rate * frame_bytes(format);
    wave.Format.cbSize = plain_header ? 0 : sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wave.Samples.wValidBitsPerSample = valid_bits(format);
    wave.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    wave.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    return wave;
}

WaveLayout layout_of(const WAVEFORMATEX& wave) {
    WaveLayout layout;
    layout.channels = wave.nChannels;
    layout.rate = wave.nSamplesPerSec;
    layout.container_bits = wave.wBitsPerSample;
    layout.valid_bits = wave.wBitsPerSample;
    layout.block_align = wave.nBlockAlign;
    if (wave.wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        wave.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto& extended = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(wave);
        layout.valid_bits = extended.Samples.wValidBitsPerSample;
        layout.integer_pcm = IsEqualGUID(extended.SubFormat, KSDATAFORMAT_SUBTYPE_PCM) != 0;
    } else {
        layout.integer_pcm = wave.wFormatTag == WAVE_FORMAT_PCM;
    }
    return layout;
}

// One opened device and the mix on its way into it. Lives on the sink's thread.
class Playback {
public:
    Playback(const SinkRequest& request, WasapiOptions options, HANDLE wake)
        : request_(request), options_(options), exclusive_(request.sharing == OutputSharing::Exclusive), wake_(wake) {}

    // Opens the default device, fills its buffer for the first time and starts it.
    std::optional<Failure> start() {
        ComRef<IMMDeviceEnumerator> devices;
        HRESULT code = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, IID_IMMDeviceEnumerator,
                                        devices.untyped_slot());
        if (FAILED(code)) return failure_of(code, "WASAPI: the device enumerator is not available");
        if (FAILED(devices->GetDefaultAudioEndpoint(eRender, eConsole, endpoint_.slot()))) {
            return Failure{OutputResult::NoDevice, 0, "WASAPI: there is no default playback device"};
        }

        code = exclusive_ ? negotiate_exclusive() : negotiate_shared();
        if (FAILED(code)) {
            return failure_of(code, exclusive_ ? "WASAPI: the device did not open for exclusive use in a format of the mix"
                                               : "WASAPI: the shared device does not mix in the mix's own format");
        }
        code = client_->SetEventHandle(wake_);
        if (FAILED(code)) return failure_of(code, "WASAPI: the device did not take the wake event");
        code = client_->GetService(IID_IAudioRenderClient, buffer_.untyped_slot());
        if (FAILED(code)) return failure_of(code, "WASAPI: the device gave no render buffer");
        // The play position is optional: without it the mix only keeps ahead of what was written.
        client_->GetService(IID_IAudioClock, clock_.untyped_slot());
        if (clock_ && FAILED(clock_->GetFrequency(&clock_frequency_))) clock_frequency_ = 0;

        repeat_ = rate_multiple(request_.device_rate);
        const std::uint32_t buffer_mix_frames = (buffer_frames_ + repeat_ - 1) / repeat_;
        const std::uint32_t queue_frames = std::max(request_.queue_frames, buffer_mix_frames * 2 + 1);
        lead_frames_ = std::min(queue_frames - 1, std::max(buffer_mix_frames * 2, kLeastLeadFrames));
        feed_.emplace(request_.source, queue_frames, request_.board->counters());
        writer_.emplace(*feed_, format_, repeat_);

        OpenedDevice opened;
        opened.sink = OutputSink::Wasapi;
        opened.sharing = request_.sharing;
        opened.format = format_;
        opened.device_rate = request_.device_rate;
        opened.bit_perfect = exclusive_ && repeat_ == 1;
        opened.name = friendly_name(endpoint_.get());
        request_.board->opened(opened);
        request_.board->set_state(OutputState::Priming);

        feed_->render_until(std::max<std::uint64_t>(lead_frames_, writer_->mix_frames_for(buffer_frames_)));
        if (auto failure = write_frames(buffer_frames_)) return failure;
        code = client_->Start();
        if (FAILED(code)) return failure_of(code, "WASAPI: the device did not start");
        request_.board->set_state(OutputState::Playing);
        return std::nullopt;
    }

    // Answers the device's wake events until `stop` is signalled or the device fails.
    void serve(HANDLE stop) {
        const HANDLE signals[2] = {stop, wake_};
        for (;;) {
            const DWORD woke = WaitForMultipleObjects(2, signals, FALSE, INFINITE);
            if (woke == WAIT_OBJECT_0) break;
            if (woke != WAIT_OBJECT_0 + 1) {
                request_.board->fail(OutputResult::SinkError, GetLastError(), "WASAPI: waiting for the device failed");
                break;
            }
            if (auto failure = refill()) {
                request_.board->fail(failure->result, failure->native, failure->note);
                break;
            }
        }
        client_->Stop();
    }

private:
    HRESULT activate_client() {
        return endpoint_->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, client_.untyped_slot());
    }

    HRESULT negotiate_exclusive() {
        HRESULT code = activate_client();
        if (FAILED(code)) return code;

        WAVEFORMATEXTENSIBLE wave{};
        bool accepted = false;
        for (const ExclusiveOffer& offer : exclusive_offers()) {
            wave = describe(offer.format, request_.device_rate, offer.plain_header);
            if (client_->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &wave.Format, nullptr) == S_OK) {
                format_ = offer.format;
                accepted = true;
                break;
            }
        }
        if (!accepted) return AUDCLNT_E_UNSUPPORTED_FORMAT;

        REFERENCE_TIME usual_period = 0;
        REFERENCE_TIME shortest_period = 0;
        code = client_->GetDevicePeriod(&usual_period, &shortest_period);
        if (FAILED(code)) return code;
        REFERENCE_TIME period = std::max<REFERENCE_TIME>(request_.latency_ms * kTicksPerMs, shortest_period);
        code = client_->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, kStreamFlags, period, period, &wave.Format, nullptr);
        if (code == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
            // The device wants a whole number of its own blocks: it tells the nearest size, and a new client is
            // opened with the period of exactly that many frames.
            UINT32 aligned_frames = 0;
            if (FAILED(client_->GetBufferSize(&aligned_frames))) return code;
            period = static_cast<REFERENCE_TIME>((kTicksPerSecond * aligned_frames + request_.device_rate - 1) /
                                                 request_.device_rate);
            code = activate_client();
            if (FAILED(code)) return code;
            code = client_->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, kStreamFlags, period, period, &wave.Format,
                                       nullptr);
        }
        if (FAILED(code)) return code;
        return client_->GetBufferSize(&buffer_frames_);
    }

    HRESULT negotiate_shared() {
        HRESULT code = activate_client();
        if (FAILED(code)) return code;
        WAVEFORMATEX* mixer_format = nullptr;
        code = client_->GetMixFormat(&mixer_format);
        if (FAILED(code)) return code;
        const std::unique_ptr<WAVEFORMATEX, TaskMemoryFree> owned(mixer_format);
        const std::optional<SampleFormat> format = format_for_layout(layout_of(*mixer_format), request_.device_rate);
        const REFERENCE_TIME buffer = request_.latency_ms * kTicksPerMs;
        if (format) {
            format_ = *format;
            code = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, kStreamFlags, buffer, 0, mixer_format, nullptr);
        } else if (options_.windows_converts) {
            format_ = SampleFormat::S16;
            const WAVEFORMATEXTENSIBLE wave = describe(format_, request_.device_rate, false);
            code = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, kStreamFlags | kConvertFlags, buffer, 0, &wave.Format,
                                       nullptr);
        } else {
            return AUDCLNT_E_UNSUPPORTED_FORMAT;
        }
        if (FAILED(code)) return code;
        return client_->GetBufferSize(&buffer_frames_);
    }

    // Device frames played since the first wake event, by the device's own clock; 0 without one.
    std::uint64_t frames_played() {
        if (!clock_ || clock_frequency_ == 0) return 0;
        UINT64 position = 0;
        if (FAILED(clock_->GetPosition(&position, nullptr))) return 0;
        if (!clock_origin_) clock_origin_ = position;
        const UINT64 elapsed = position - *clock_origin_;
        const std::uint64_t rate = request_.device_rate;
        const std::uint64_t played =
            elapsed / clock_frequency_ * rate + elapsed % clock_frequency_ * rate / clock_frequency_;
        request_.board->counters().played.store(played, std::memory_order_relaxed);
        return played;
    }

    std::optional<Failure> refill() {
        const std::uint64_t played = frames_played();
        // An exclusive device hands over its whole buffer at every event; a shared one, what is not still queued.
        UINT32 room = buffer_frames_;
        if (!exclusive_) {
            UINT32 queued = 0;
            const HRESULT code = client_->GetCurrentPadding(&queued);
            if (FAILED(code)) return failure_of(code, "WASAPI: the device did not tell how full its buffer is");
            room = buffer_frames_ - queued;
        }
        if (room == 0) return std::nullopt;
        feed_->render_until(
            std::max<std::uint64_t>(played / repeat_ + lead_frames_, feed_->taken() + writer_->mix_frames_for(room)));
        return write_frames(room);
    }

    std::optional<Failure> write_frames(UINT32 frames) {
        BYTE* bytes = nullptr;
        HRESULT code = buffer_->GetBuffer(frames, &bytes);
        if (FAILED(code)) return failure_of(code, "WASAPI: the device's buffer could not be taken");
        writer_->write(bytes, frames);
        code = buffer_->ReleaseBuffer(frames, 0);
        if (FAILED(code)) return failure_of(code, "WASAPI: the device's buffer could not be given back");
        request_.board->counters().written.fetch_add(frames, std::memory_order_relaxed);
        return std::nullopt;
    }

    const SinkRequest& request_;
    const WasapiOptions options_;
    const bool exclusive_;
    const HANDLE wake_;
    ComRef<IMMDevice> endpoint_;
    ComRef<IAudioClient> client_;
    ComRef<IAudioRenderClient> buffer_;
    ComRef<IAudioClock> clock_;
    UINT64 clock_frequency_ = 0;
    std::optional<UINT64> clock_origin_;
    SampleFormat format_ = SampleFormat::S16;
    UINT32 buffer_frames_ = 0;
    std::uint32_t repeat_ = 1;
    std::uint32_t lead_frames_ = 0;
    std::optional<MixFeed> feed_;
    std::optional<DeviceWriter> writer_;
};

class WasapiSink final : public DeviceSink {
public:
    explicit WasapiSink(WasapiOptions options) : options_(options) {}
    ~WasapiSink() override { close(); }

    OutputResult open(const SinkRequest& request) override {
        request_ = request;
        stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);   // stays signalled once set
        wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);  // resets as the thread wakes
        if (stop_ == nullptr || wake_ == nullptr) {
            request.board->fail(OutputResult::SinkError, GetLastError(), "WASAPI: no event objects");
            release_events();
            return OutputResult::SinkError;
        }

        std::promise<OutputResult> verdict;
        std::future<OutputResult> answer = verdict.get_future();
        try {
            thread_ = std::thread([this, verdict = std::move(verdict)]() mutable { run(verdict); });
        } catch (const std::system_error&) {
            request.board->fail(OutputResult::ThreadError, 0, "WASAPI: the sound thread did not start");
            release_events();
            return OutputResult::ThreadError;
        }
        const OutputResult result = answer.get();
        if (result != OutputResult::Ok) {
            thread_.join();
            release_events();
        }
        return result;
    }

    void close() override {
        if (stop_ != nullptr) SetEvent(stop_);
        if (thread_.joinable()) thread_.join();
        release_events();
    }

private:
    // The sink's thread: the device is opened, served and closed here, so every COM call is on one thread.
    void run(std::promise<OutputResult>& verdict) {
        const ComSession com;
        if (!com.usable()) {
            request_.board->fail(result_of(com.code()), com.code(), "WASAPI: COM did not start on the sound thread");
            verdict.set_value(result_of(com.code()));
            return;
        }
        const AudioThreadPriority priority;
        Playback playback(request_, options_, wake_);
        if (const std::optional<Failure> failure = playback.start()) {
            request_.board->fail(failure->result, failure->native, failure->note);
            verdict.set_value(failure->result);
            return;
        }
        verdict.set_value(OutputResult::Ok);
        playback.serve(stop_);
    }

    void release_events() {
        if (wake_ != nullptr) CloseHandle(wake_);
        if (stop_ != nullptr) CloseHandle(stop_);
        wake_ = nullptr;
        stop_ = nullptr;
    }

    const WasapiOptions options_;
    SinkRequest request_;
    HANDLE stop_ = nullptr;
    HANDLE wake_ = nullptr;
    std::thread thread_;
};

}  // namespace

std::unique_ptr<DeviceSink> make_wasapi_sink(WasapiOptions options) { return std::make_unique<WasapiSink>(options); }

}  // namespace sh::audio::output

#endif  // _WIN32
