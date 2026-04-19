// EmuHem I/Q sample source abstraction.
// Sources plug into baseband_dma_emu.cpp to replace the default synthetic
// noise generator with file-backed or network-backed I/Q streams.

#pragma once

#include "complex.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace emuhem {

class IQSource {
   public:
    virtual ~IQSource() = default;
    // Fill `out[0..count)` with complex8_t samples. Returns how many were
    // actually written. Sources are expected to always return `count` or 0
    // (the latter only when permanently exhausted).
    virtual size_t read(complex8_t* out, size_t count) = 0;
    // Short human-readable name for logging.
    virtual const char* name() const = 0;

    // Optional upstream hooks. Default is no-op. Network-backed sources
    // override these to forward tuning state to the remote server.
    virtual void on_sample_rate_changed(uint32_t /*hz*/) {}
    virtual void on_center_frequency_changed(uint64_t /*hz*/) {}
    virtual void on_tuner_gain_changed(int32_t /*tenths_db*/) {}
};

class NoiseIQSource : public IQSource {
   public:
    NoiseIQSource(float noise_std_dev = 12.0f,
                  float test_tone_amplitude = 50.0f,
                  float test_tone_bin = 64.0f /* of 256 */);
    size_t read(complex8_t* out, size_t count) override;
    const char* name() const override { return "noise+tone"; }

   private:
    float noise_std_dev_;
    float tone_amp_;
    float tone_phase_inc_;
    float tone_phase_ = 0.0f;
};

class FileIQSource : public IQSource {
   public:
    enum class Format {
        // Signed 8-bit I/Q, interleaved (PortaPack native / HackRF .c8).
        CS8,
        // Unsigned 8-bit I/Q, interleaved (rtl_sdr .cu8). Offset by 0x80.
        CU8,
        // Signed 16-bit little-endian I/Q, interleaved (.cs16 / .C16).
        CS16,
        // Complex float32 I/Q, interleaved (.cf32 / GNU Radio raw).
        CF32,
        // RIFF .wav container with 8- or 16-bit PCM stereo = I/Q.
        WAV,
    };

    // Loads the entire file into memory as complex8_t. Returns nullptr on
    // failure, logging the reason to stderr.
    static std::unique_ptr<FileIQSource> open(const std::string& path, bool loop);

    size_t read(complex8_t* out, size_t count) override;
    const char* name() const override { return display_name_.c_str(); }
    size_t sample_count() const { return samples_.size(); }

   private:
    FileIQSource() = default;

    std::vector<complex8_t> samples_;
    size_t cursor_ = 0;
    bool loop_ = true;
    bool exhausted_ = false;
    std::string display_name_;
};

// Streams cu8 samples from a remote rtl_tcp server (e.g., `rtl_tcp -a 0.0.0.0`
// driving a real rtl-sdr dongle, or any rtl_tcp-compatible server). Samples
// are converted on the fly from unsigned cu8 to signed cs8 (XOR 0x80). A
// background receiver thread fills a bounded ring buffer; `read()` drains it
// and zero-pads if the network falls behind so the baseband DMA never stalls.
class RtlTcpClientSource : public IQSource {
   public:
    // Connect to `host:port` and read the 12-byte dongle header. Returns
    // nullptr on any failure, logging the reason. On success, spawns a
    // receiver thread that runs until destruction.
    static std::unique_ptr<RtlTcpClientSource> open(const std::string& host, uint16_t port);

    ~RtlTcpClientSource();
    size_t read(complex8_t* out, size_t count) override;
    const char* name() const override { return display_name_.c_str(); }

    // Forward tuning to the remote rtl_tcp server via the standard 5-byte
    // command packets. Silently dropped if the socket is gone.
    void on_sample_rate_changed(uint32_t hz) override;
    void on_center_frequency_changed(uint64_t hz) override;
    void on_tuner_gain_changed(int32_t tenths_db) override;

   private:
    RtlTcpClientSource() = default;
    void receiver_loop();
    void send_command(uint8_t cmd, uint32_t param);

    int sockfd_ = -1;
    std::atomic<bool> stop_{false};
    std::thread thread_;
    std::string display_name_;
    std::mutex send_mutex_;  // serializes upstream command writes

    // Last forwarded values, to suppress spammy duplicate commands.
    std::atomic<uint32_t> last_sample_rate_{0};
    std::atomic<uint32_t> last_frequency_{0};
    std::atomic<int32_t> last_gain_{0x7fffffff};

    // Ring buffer (single producer / single consumer, mutex-guarded for
    // simplicity -- sample rates are low enough that a lock-free ring isn't
    // needed yet).
    std::vector<complex8_t> ring_;
    size_t head_ = 0;  // write cursor (producer)
    size_t tail_ = 0;  // read cursor (consumer)
    size_t count_ = 0;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<uint64_t> dropped_samples_{0};
};

#ifdef EMUHEM_HAS_SOAPYSDR
// Streams live I/Q from any SoapySDR-supported USB dongle (HackRF, RTL-SDR,
// Airspy, LimeSDR, PlutoSDR, BladeRF, etc.). Opens the device with the
// user-supplied Soapy arg string (e.g. "driver=hackrf,serial=abc"), negotiates
// a sample format (prefers CS8 for direct memcpy, falls back to CS16 or CF32),
// and spawns a receiver thread that drains the stream into a bounded ring
// buffer. `read()` copies from the ring and zero-pads on under-run so the
// baseband DMA never stalls.
//
// Tuning state is pushed down to the device via the on_* hooks: this is a
// "network-tuned" source in the sense that the dongle handles its own
// centering, so make_default_source() does NOT wrap it in the NCO shifter.
class SoapyIQSource : public IQSource {
   public:
    struct OpenDefaults {
        double sample_rate_hz = 2'400'000.0;
        double frequency_hz = 100'000'000.0;
        double gain_db = 20.0;  // auto-like; most devices accept this
    };

    static std::unique_ptr<SoapyIQSource> open(const std::string& args,
                                               const OpenDefaults& defaults);

    ~SoapyIQSource();
    size_t read(complex8_t* out, size_t count) override;
    const char* name() const override { return display_name_.c_str(); }

    void on_sample_rate_changed(uint32_t hz) override;
    void on_center_frequency_changed(uint64_t hz) override;
    void on_tuner_gain_changed(int32_t tenths_db) override;

   private:
    SoapyIQSource() = default;
    void receiver_loop();

    // Opaque to avoid pulling SoapySDR headers into every TU.
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::atomic<bool> stop_{false};
    std::thread thread_;
    std::string display_name_;

    // Dedup upstream commands on repeated identical calls.
    std::atomic<uint32_t> last_sample_rate_{0};
    std::atomic<uint64_t> last_frequency_{0};
    std::atomic<int32_t> last_gain_{0x7fffffff};

    // Ring buffer — same pattern as RtlTcpClientSource.
    std::vector<complex8_t> ring_;
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t count_ = 0;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<uint64_t> dropped_samples_{0};
};
#endif  // EMUHEM_HAS_SOAPYSDR

// Wraps an inner IQSource and mixes its samples with a numerically-controlled
// oscillator so a file (or noise) recorded at some "declared" center frequency
// appears at the right offset in the spectrum when the user tunes the virtual
// radio elsewhere.
//
// Model: the inner source produces baseband samples representing RF energy
// centered at `declared_center_hz_`. When the user tunes to `tuned_hz` via
// on_center_frequency_changed, we multiply each sample by
// exp(j*2*pi*(declared - tuned)*t/fs) so that a tone originally at baseband
// offset k (i.e., real freq declared+k) lands at offset (declared+k-tuned) in
// the delivered spectrum.
//
// If `declared_center_hz_` is 0 or the tuned frequency has never been set,
// the source acts as a transparent passthrough.
class FrequencyShiftingSource : public IQSource {
   public:
    FrequencyShiftingSource(std::unique_ptr<IQSource> inner,
                            uint64_t declared_center_hz,
                            uint32_t initial_sample_rate = 0);
    size_t read(complex8_t* out, size_t count) override;
    const char* name() const override { return display_name_.c_str(); }

    void on_sample_rate_changed(uint32_t hz) override;
    void on_center_frequency_changed(uint64_t hz) override;
    void on_tuner_gain_changed(int32_t tenths_db) override;

   private:
    void recompute_phase_inc_locked();

    std::unique_ptr<IQSource> inner_;
    uint64_t declared_center_hz_;
    std::string display_name_;

    std::mutex mutex_;
    std::atomic<uint32_t> sample_rate_{0};
    std::atomic<uint64_t> tuned_hz_{0};
    std::atomic<bool> tuned_set_{false};

    // NCO state: 32-bit fixed-point phase accumulator. Phase increment is
    // 2^32 * shift_hz / sample_rate (signed: negative shift -> negative inc).
    uint32_t phase_ = 0;
    int32_t phase_inc_ = 0;  // cached from last recompute
};

// rtl_tcp-protocol compatible server. Accepts external SDR clients
// (gqrx, SDR++, GNU Radio) and fans out the current baseband I/Q stream
// as cu8. Upstream commands from clients are logged; `set_sample_rate` is
// honored (forwarded to baseband::dma::set_sample_rate).
class RtlTcpServer {
   public:
    // Bind to `bind_host:port` and start the accept thread. Returns nullptr
    // on bind/listen failure. `bind_host` may be empty for INADDR_ANY.
    static std::unique_ptr<RtlTcpServer> start(const std::string& bind_host, uint16_t port);
    ~RtlTcpServer();

    // Fan out a freshly-read baseband buffer to all connected clients.
    // Each client has its own bounded ring; overflow drops oldest samples.
    // Call rate should roughly match the baseband DMA transfer rate.
    void push(const complex8_t* samples, size_t count);

    // True if at least one client is currently connected. Baseband DMA
    // can use this to skip the per-buffer fanout when no one's listening.
    bool has_clients() const { return client_count_.load(std::memory_order_relaxed) > 0; }

   private:
    RtlTcpServer() = default;
    void accept_loop();

    struct Client;
    void client_send_loop(Client* c);
    void client_recv_loop(Client* c);

    int listen_fd_ = -1;
    uint16_t bound_port_ = 0;
    std::atomic<bool> stop_{false};
    std::thread accept_thread_;
    std::atomic<size_t> client_count_{0};

    struct Client {
        int fd = -1;
        std::thread send_thread;
        std::thread recv_thread;
        std::atomic<bool> stop{false};
        std::vector<uint8_t> ring;  // cu8 bytes (2 per sample)
        size_t head = 0, tail = 0, count = 0;
        std::mutex mutex;
        std::condition_variable cv;
        std::atomic<uint64_t> dropped_samples{0};
    };

    // List of live clients. We keep them in unique_ptr so addresses stay
    // stable across vector reallocation, which matters because the client
    // threads hold raw Client* pointers.
    std::mutex clients_mutex_;
    std::vector<std::unique_ptr<Client>> clients_;
};

// ============================================================================
// IQSink — mirror of IQSource for the TX path.
// ============================================================================
//
// A TX baseband processor's execute() writes complex8_t samples into the
// buffer handed to it by baseband::dma::wait_for_buffer(). EmuHem drains that
// filled buffer into an IQSink. Sinks are plugged in the same way as sources:
// created by make_default_sink() based on environment variables, instantiated
// lazily on the first TX wait_for_buffer() call.

class IQSink {
   public:
    virtual ~IQSink() = default;
    // Consume `in[0..count)` complex8_t TX samples produced by the processor.
    // Implementations are expected to accept every sample; back-pressure, if
    // any, happens inside the sink (e.g. a bounded ring buffer that drops
    // oldest when the downstream writer can't keep up).
    virtual void write(const complex8_t* in, size_t count) = 0;
    virtual const char* name() const = 0;

    // Optional downstream hooks. Default no-op. Network / device-backed sinks
    // (SoapySDR) override these to apply TX tuning on the underlying device.
    virtual void on_sample_rate_changed(uint32_t /*hz*/) {}
    virtual void on_center_frequency_changed(uint64_t /*hz*/) {}
    virtual void on_tx_gain_changed(int32_t /*tenths_db*/) {}
};

// Discards every sample. Logs a dropped-count periodically (every N calls)
// so the user is not surprised by silence when no real sink is attached.
class NullIQSink : public IQSink {
   public:
    void write(const complex8_t* in, size_t count) override;
    const char* name() const override { return "null (discarded)"; }

   private:
    uint64_t dropped_samples_ = 0;
    uint64_t next_log_at_ = 1'000'000;  // first report after 1 M samples
};

// Appends CS8 bytes to a file. Format matches FileIQSource::Format::CS8 so
// the output can be fed straight back in via --iq-file=. No looping, no
// container header — raw interleaved int8 I/Q pairs.
class FileIQSink : public IQSink {
   public:
    static std::unique_ptr<FileIQSink> open(const std::string& path);
    ~FileIQSink();

    void write(const complex8_t* in, size_t count) override;
    const char* name() const override { return display_name_.c_str(); }

   private:
    FileIQSink() = default;
    std::FILE* fp_ = nullptr;
    std::string display_name_;
    uint64_t bytes_written_ = 0;
};

#ifdef EMUHEM_HAS_SOAPYSDR
// Pushes TX samples to a SoapySDR-supported device (HackRF, PlutoSDR, LimeSDR,
// BladeRF, etc.). Opens the device with the user-supplied arg string, picks
// the best TX stream format (prefers CS8 for direct memcpy, falls back to
// CS16 or CF32), and spawns a writer thread that drains a bounded ring buffer
// into the device via writeStream. Tuning state is pushed via on_* hooks.
class SoapyIQSink : public IQSink {
   public:
    struct OpenDefaults {
        double sample_rate_hz = 2'400'000.0;
        double frequency_hz = 100'000'000.0;
        double gain_db = 20.0;
    };

    static std::unique_ptr<SoapyIQSink> open(const std::string& args,
                                             const OpenDefaults& defaults);

    ~SoapyIQSink();
    void write(const complex8_t* in, size_t count) override;
    const char* name() const override { return display_name_.c_str(); }

    void on_sample_rate_changed(uint32_t hz) override;
    void on_center_frequency_changed(uint64_t hz) override;
    void on_tx_gain_changed(int32_t tenths_db) override;

   private:
    SoapyIQSink() = default;
    void writer_loop();

    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::atomic<bool> stop_{false};
    std::thread thread_;
    std::string display_name_;

    std::atomic<uint32_t> last_sample_rate_{0};
    std::atomic<uint64_t> last_frequency_{0};
    std::atomic<int32_t> last_gain_{0x7fffffff};

    // Ring of complex8_t samples; writer thread drains, producer (write())
    // enqueues. Overflow drops oldest.
    std::vector<complex8_t> ring_;
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t count_ = 0;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<uint64_t> dropped_samples_{0};
};
#endif  // EMUHEM_HAS_SOAPYSDR

// Create the default sink based on environment:
//   EMUHEM_IQ_TX_FILE=/path/to/out.c8     -> FileIQSink (CS8)
//   EMUHEM_IQ_TX_SOAPY=<soapy args>       -> SoapyIQSink (e.g. "driver=hackrf")
//   EMUHEM_IQ_TX_SOAPY_RATE / _FREQ / _GAIN -> startup defaults for the dongle
//   otherwise NullIQSink (samples discarded).
// Precedence: IQ_TX_FILE > IQ_TX_SOAPY > Null.
std::unique_ptr<IQSink> make_default_sink();

// Create the default source based on environment:
//   EMUHEM_IQ_FILE=/path/to/capture.c8   -> FileIQSource
//   EMUHEM_IQ_LOOP=0                      -> disable looping (default: loop)
//   EMUHEM_IQ_SOAPY=<soapy args>          -> SoapyIQSource (e.g. "driver=hackrf")
//   EMUHEM_IQ_SOAPY_RATE / _FREQ / _GAIN  -> startup defaults for the dongle
//   EMUHEM_IQ_TCP=host:port               -> RtlTcpClientSource
//   otherwise NoiseIQSource
// Precedence: IQ_FILE > IQ_SOAPY > IQ_TCP > Noise.
//
// If EMUHEM_IQ_CENTER=<hz> is set (non-zero) and the resulting source is not
// already a network-tuned client (RtlTcpClientSource handles its own tuning),
// the source is wrapped in a FrequencyShiftingSource so UI tuning shifts the
// spectrum relative to the declared recording center.
std::unique_ptr<IQSource> make_default_source();

// Create the rtl_tcp server based on environment:
//   EMUHEM_RTL_TCP_SERVER=host:port       -> RtlTcpServer (host may be empty)
// Returns nullptr if the env var isn't set or the bind fails.
std::unique_ptr<RtlTcpServer> make_default_server();

}  // namespace emuhem
