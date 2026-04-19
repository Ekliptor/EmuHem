// EmuHem I/Q source implementations.

#include "iq_source.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <random>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace emuhem {

// ============================================================================
// NoiseIQSource
// ============================================================================

NoiseIQSource::NoiseIQSource(float noise_std_dev, float test_tone_amplitude, float test_tone_bin)
    : noise_std_dev_(noise_std_dev), tone_amp_(test_tone_amplitude) {
    constexpr float kTwoPi = 2.0f * 3.14159265358979f;
    tone_phase_inc_ = kTwoPi * test_tone_bin / 256.0f;
}

size_t NoiseIQSource::read(complex8_t* out, size_t count) {
    thread_local std::mt19937 rng{std::random_device{}()};
    std::normal_distribution<float> dist{0.0f, noise_std_dev_};

    for (size_t i = 0; i < count; ++i) {
        float re = dist(rng);
        float im = dist(rng);
        if (tone_amp_ > 0.0f) {
            re += tone_amp_ * std::cos(tone_phase_);
            im += tone_amp_ * std::sin(tone_phase_);
            tone_phase_ += tone_phase_inc_;
            constexpr float kTwoPi = 2.0f * 3.14159265358979f;
            if (tone_phase_ > kTwoPi) tone_phase_ -= kTwoPi;
        }
        auto clamp8 = [](float v) -> int8_t {
            if (v > 127.0f) return 127;
            if (v < -127.0f) return -127;
            return static_cast<int8_t>(v);
        };
        out[i] = {clamp8(re), clamp8(im)};
    }
    return count;
}

// ============================================================================
// FileIQSource
// ============================================================================

namespace {

std::string to_lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::optional<FileIQSource::Format> detect_format(const std::string& path) {
    namespace fs = std::filesystem;
    const std::string ext = to_lower(fs::path{path}.extension().string());
    if (ext == ".c8" || ext == ".cs8") return FileIQSource::Format::CS8;
    if (ext == ".cu8" || ext == ".u8") return FileIQSource::Format::CU8;
    if (ext == ".c16" || ext == ".cs16") return FileIQSource::Format::CS16;
    if (ext == ".cf32" || ext == ".fc32" || ext == ".raw") return FileIQSource::Format::CF32;
    if (ext == ".wav") return FileIQSource::Format::WAV;
    return std::nullopt;
}

int8_t clamp_to_i8(float v) {
    if (v > 127.0f) return 127;
    if (v < -127.0f) return -127;
    return static_cast<int8_t>(v);
}

// Minimal RIFF/WAV I/Q parser. Accepts stereo 8-bit unsigned or 16-bit signed
// PCM (channels 1=I, 2=Q). Returns false on any unsupported container.
bool decode_wav_to_cs8(const std::vector<uint8_t>& raw,
                      std::vector<complex8_t>& out) {
    auto rd_u32 = [&](size_t off) -> uint32_t {
        return uint32_t(raw[off]) | (uint32_t(raw[off + 1]) << 8)
             | (uint32_t(raw[off + 2]) << 16) | (uint32_t(raw[off + 3]) << 24);
    };
    auto rd_u16 = [&](size_t off) -> uint16_t {
        return uint16_t(raw[off]) | (uint16_t(raw[off + 1]) << 8);
    };

    if (raw.size() < 44) return false;
    if (std::memcmp(raw.data(), "RIFF", 4) != 0) return false;
    if (std::memcmp(raw.data() + 8, "WAVE", 4) != 0) return false;

    // Walk chunks looking for 'fmt ' and 'data'.
    size_t pos = 12;
    uint16_t fmt_code = 0, channels = 0, bits = 0;
    size_t data_off = 0, data_len = 0;

    while (pos + 8 <= raw.size()) {
        const char* tag = reinterpret_cast<const char*>(raw.data() + pos);
        const uint32_t sz = rd_u32(pos + 4);
        const size_t body = pos + 8;
        if (std::memcmp(tag, "fmt ", 4) == 0 && sz >= 16 && body + 16 <= raw.size()) {
            fmt_code = rd_u16(body);
            channels = rd_u16(body + 2);
            bits     = rd_u16(body + 14);
        } else if (std::memcmp(tag, "data", 4) == 0) {
            data_off = body;
            data_len = std::min<size_t>(sz, raw.size() - body);
            break;
        }
        pos = body + sz + (sz & 1);  // chunks are word-aligned
    }

    if (fmt_code != 1 /*PCM*/ || channels != 2 || data_len == 0) {
        std::fprintf(stderr, "[EmuHem] iq_source: wav needs PCM stereo (got fmt=%u ch=%u bits=%u)\n",
                     fmt_code, channels, bits);
        return false;
    }

    const uint8_t* d = raw.data() + data_off;
    if (bits == 16) {
        const size_t n = data_len / 4;  // 2 ch * 2 bytes
        out.resize(n);
        for (size_t i = 0; i < n; ++i) {
            const int16_t si = static_cast<int16_t>(d[4 * i] | (d[4 * i + 1] << 8));
            const int16_t sq = static_cast<int16_t>(d[4 * i + 2] | (d[4 * i + 3] << 8));
            out[i] = {static_cast<int8_t>(si >> 8), static_cast<int8_t>(sq >> 8)};
        }
        return true;
    }
    if (bits == 8) {
        const size_t n = data_len / 2;
        out.resize(n);
        for (size_t i = 0; i < n; ++i) {
            out[i] = {static_cast<int8_t>(d[2 * i] ^ 0x80),
                      static_cast<int8_t>(d[2 * i + 1] ^ 0x80)};
        }
        return true;
    }
    std::fprintf(stderr, "[EmuHem] iq_source: wav bit depth %u unsupported\n", bits);
    return false;
}

}  // namespace

std::unique_ptr<FileIQSource> FileIQSource::open(const std::string& path, bool loop) {
    auto fmt = detect_format(path);
    if (!fmt) {
        std::fprintf(stderr, "[EmuHem] iq_source: unsupported file extension for %s\n", path.c_str());
        return nullptr;
    }

    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        std::fprintf(stderr, "[EmuHem] iq_source: fopen(%s) failed: %s\n", path.c_str(), std::strerror(errno));
        return nullptr;
    }

    std::fseek(fp, 0, SEEK_END);
    const long file_size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (file_size <= 0) {
        std::fclose(fp);
        std::fprintf(stderr, "[EmuHem] iq_source: %s is empty\n", path.c_str());
        return nullptr;
    }

    auto src = std::unique_ptr<FileIQSource>(new FileIQSource{});
    src->loop_ = loop;
    src->display_name_ = "file:" + std::filesystem::path{path}.filename().string();

    std::vector<uint8_t> raw(static_cast<size_t>(file_size));
    if (std::fread(raw.data(), 1, raw.size(), fp) != raw.size()) {
        std::fclose(fp);
        std::fprintf(stderr, "[EmuHem] iq_source: short read from %s\n", path.c_str());
        return nullptr;
    }
    std::fclose(fp);

    switch (*fmt) {
        case Format::CS8: {
            const size_t n = raw.size() / 2;
            src->samples_.resize(n);
            std::memcpy(src->samples_.data(), raw.data(), n * 2);
            break;
        }
        case Format::CU8: {
            const size_t n = raw.size() / 2;
            src->samples_.resize(n);
            for (size_t i = 0; i < n; ++i) {
                src->samples_[i] = {
                    static_cast<int8_t>(raw[2 * i] ^ 0x80),
                    static_cast<int8_t>(raw[2 * i + 1] ^ 0x80),
                };
            }
            break;
        }
        case Format::CS16: {
            const size_t n = raw.size() / 4;
            src->samples_.resize(n);
            const auto* p = reinterpret_cast<const int16_t*>(raw.data());
            for (size_t i = 0; i < n; ++i) {
                src->samples_[i] = {
                    static_cast<int8_t>(p[2 * i] >> 8),
                    static_cast<int8_t>(p[2 * i + 1] >> 8),
                };
            }
            break;
        }
        case Format::CF32: {
            const size_t n = raw.size() / 8;  // 2 floats per sample
            src->samples_.resize(n);
            const auto* p = reinterpret_cast<const float*>(raw.data());
            // Normalized float I/Q is typically in [-1, 1]; scale to cs8 full scale.
            for (size_t i = 0; i < n; ++i) {
                src->samples_[i] = {
                    clamp_to_i8(p[2 * i] * 127.0f),
                    clamp_to_i8(p[2 * i + 1] * 127.0f),
                };
            }
            break;
        }
        case Format::WAV: {
            if (!decode_wav_to_cs8(raw, src->samples_)) {
                return nullptr;
            }
            break;
        }
    }

    if (src->samples_.empty()) {
        std::fprintf(stderr, "[EmuHem] iq_source: %s decoded to 0 samples\n", path.c_str());
        return nullptr;
    }

    std::fprintf(stderr, "[EmuHem] iq_source: loaded %zu samples from %s (loop=%d)\n",
                 src->samples_.size(), path.c_str(), loop ? 1 : 0);
    return src;
}

size_t FileIQSource::read(complex8_t* out, size_t count) {
    if (exhausted_) return 0;

    size_t written = 0;
    while (written < count) {
        const size_t available = samples_.size() - cursor_;
        const size_t take = std::min(available, count - written);
        std::memcpy(out + written, samples_.data() + cursor_, take * sizeof(complex8_t));
        cursor_ += take;
        written += take;

        if (cursor_ >= samples_.size()) {
            if (loop_) {
                cursor_ = 0;
            } else {
                exhausted_ = true;
                // Zero-pad remaining to keep caller pacing predictable.
                if (written < count) {
                    std::memset(out + written, 0, (count - written) * sizeof(complex8_t));
                    written = count;
                }
                break;
            }
        }
    }
    return written;
}

// ============================================================================
// RtlTcpClientSource
// ============================================================================

namespace {

constexpr size_t kRtlRingCapacity = 262144;  // ~85 ms at 3.072 MS/s

// Read `len` bytes from `fd` with retries on EINTR. Returns false on EOF or error.
bool read_exact(int fd, void* buf, size_t len) {
    auto* p = static_cast<uint8_t*>(buf);
    size_t got = 0;
    while (got < len) {
        const ssize_t n = ::recv(fd, p + got, len - got, 0);
        if (n > 0) {
            got += static_cast<size_t>(n);
            continue;
        }
        if (n == 0) return false;                  // peer closed
        if (errno == EINTR) continue;
        return false;
    }
    return true;
}

bool write_exact(int fd, const void* buf, size_t len) {
    const auto* p = static_cast<const uint8_t*>(buf);
    size_t put = 0;
    while (put < len) {
        const ssize_t n = ::send(fd, p + put, len - put, 0);
        if (n > 0) {
            put += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

}  // namespace

std::unique_ptr<RtlTcpClientSource> RtlTcpClientSource::open(const std::string& host, uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    const std::string port_s = std::to_string(port);
    addrinfo* res = nullptr;
    if (const int rc = ::getaddrinfo(host.c_str(), port_s.c_str(), &hints, &res); rc != 0) {
        std::fprintf(stderr, "[EmuHem] iq_source: getaddrinfo(%s:%u) failed: %s\n",
                     host.c_str(), port, ::gai_strerror(rc));
        return nullptr;
    }
    std::unique_ptr<addrinfo, void (*)(addrinfo*)> res_guard{res, &::freeaddrinfo};

    int fd = -1;
    for (auto* p = res; p; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    if (fd < 0) {
        std::fprintf(stderr, "[EmuHem] iq_source: connect(%s:%u) failed: %s\n",
                     host.c_str(), port, std::strerror(errno));
        return nullptr;
    }

    uint8_t hdr[12];
    if (!read_exact(fd, hdr, sizeof(hdr))) {
        std::fprintf(stderr, "[EmuHem] iq_source: rtl_tcp header read failed\n");
        ::close(fd);
        return nullptr;
    }
    if (std::memcmp(hdr, "RTL0", 4) != 0) {
        std::fprintf(stderr, "[EmuHem] iq_source: rtl_tcp magic mismatch (got %02x %02x %02x %02x)\n",
                     hdr[0], hdr[1], hdr[2], hdr[3]);
        ::close(fd);
        return nullptr;
    }
    const uint32_t tuner_type = (uint32_t(hdr[4]) << 24) | (uint32_t(hdr[5]) << 16) |
                                (uint32_t(hdr[6]) << 8)  | uint32_t(hdr[7]);
    const uint32_t num_gains  = (uint32_t(hdr[8]) << 24) | (uint32_t(hdr[9]) << 16) |
                                (uint32_t(hdr[10]) << 8) | uint32_t(hdr[11]);

    auto src = std::unique_ptr<RtlTcpClientSource>(new RtlTcpClientSource{});
    src->sockfd_ = fd;
    src->ring_.resize(kRtlRingCapacity);
    src->display_name_ = "rtl_tcp:" + host + ":" + port_s;

    std::fprintf(stderr, "[EmuHem] iq_source: rtl_tcp connected to %s:%u (tuner=%u, gains=%u)\n",
                 host.c_str(), port, tuner_type, num_gains);

    src->thread_ = std::thread([s = src.get()] { s->receiver_loop(); });
    return src;
}

RtlTcpClientSource::~RtlTcpClientSource() {
    stop_.store(true);
    if (sockfd_ >= 0) {
        ::shutdown(sockfd_, SHUT_RDWR);
        ::close(sockfd_);
        sockfd_ = -1;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    if (const uint64_t d = dropped_samples_.load(); d > 0) {
        std::fprintf(stderr, "[EmuHem] iq_source: rtl_tcp dropped %llu samples due to ring overflow\n",
                     static_cast<unsigned long long>(d));
    }
}

void RtlTcpClientSource::receiver_loop() {
    constexpr size_t kChunkBytes = 4096;  // 2048 cu8 pairs
    uint8_t buf[kChunkBytes];

    while (!stop_.load()) {
        const ssize_t n = ::recv(sockfd_, buf, sizeof(buf), 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            if (!stop_.load()) {
                std::fprintf(stderr, "[EmuHem] iq_source: rtl_tcp recv ended (%s)\n",
                             n == 0 ? "peer closed" : std::strerror(errno));
            }
            break;
        }
        // cu8 -> cs8 via XOR 0x80; pair bytes into complex8_t.
        const size_t pairs = static_cast<size_t>(n) / 2;
        std::lock_guard<std::mutex> lk(mutex_);
        for (size_t i = 0; i < pairs; ++i) {
            if (count_ >= ring_.size()) {
                // Ring full: drop the oldest pair to keep latency bounded.
                tail_ = (tail_ + 1) % ring_.size();
                --count_;
                dropped_samples_.fetch_add(1, std::memory_order_relaxed);
            }
            ring_[head_] = {
                static_cast<int8_t>(buf[2 * i] ^ 0x80),
                static_cast<int8_t>(buf[2 * i + 1] ^ 0x80),
            };
            head_ = (head_ + 1) % ring_.size();
            ++count_;
        }
        cv_.notify_one();
    }
}

size_t RtlTcpClientSource::read(complex8_t* out, size_t count) {
    std::unique_lock<std::mutex> lk(mutex_);
    const size_t take = std::min(count, count_);
    for (size_t i = 0; i < take; ++i) {
        out[i] = ring_[tail_];
        tail_ = (tail_ + 1) % ring_.size();
    }
    count_ -= take;
    // Zero-pad if the network is behind, so DMA pacing stays predictable.
    if (take < count) {
        std::memset(out + take, 0, (count - take) * sizeof(complex8_t));
    }
    return count;
}

void RtlTcpClientSource::send_command(uint8_t cmd, uint32_t param) {
    if (sockfd_ < 0 || stop_.load()) return;
    // rtl_tcp command packet: 1 byte cmd + 4 bytes param (big-endian).
    uint8_t pkt[5];
    pkt[0] = cmd;
    pkt[1] = static_cast<uint8_t>(param >> 24);
    pkt[2] = static_cast<uint8_t>(param >> 16);
    pkt[3] = static_cast<uint8_t>(param >> 8);
    pkt[4] = static_cast<uint8_t>(param);
    std::lock_guard<std::mutex> lk(send_mutex_);
    if (!write_exact(sockfd_, pkt, sizeof(pkt))) {
        std::fprintf(stderr, "[EmuHem] iq_source: rtl_tcp send cmd 0x%02x failed: %s\n",
                     cmd, std::strerror(errno));
    }
}

void RtlTcpClientSource::on_sample_rate_changed(uint32_t hz) {
    if (hz == 0) return;
    if (last_sample_rate_.exchange(hz) == hz) return;
    std::fprintf(stderr, "[EmuHem] iq_source: rtl_tcp -> set_sample_rate(%u)\n", hz);
    send_command(0x02, hz);
}

void RtlTcpClientSource::on_center_frequency_changed(uint64_t hz) {
    const uint32_t hz32 = (hz > UINT32_MAX) ? UINT32_MAX : static_cast<uint32_t>(hz);
    if (hz32 == 0) return;
    if (last_frequency_.exchange(hz32) == hz32) return;
    std::fprintf(stderr, "[EmuHem] iq_source: rtl_tcp -> set_frequency(%u)\n", hz32);
    send_command(0x01, hz32);
}

void RtlTcpClientSource::on_tuner_gain_changed(int32_t tenths_db) {
    if (last_gain_.exchange(tenths_db) == tenths_db) return;
    std::fprintf(stderr, "[EmuHem] iq_source: rtl_tcp -> set_tuner_gain(%d/10 dB)\n", tenths_db);
    send_command(0x04, static_cast<uint32_t>(tenths_db));
}

// ============================================================================
// RtlTcpServer
// ============================================================================

namespace {

constexpr size_t kServerClientRingBytes = 262144 * 2;  // ~85 ms of cu8 at 3.072 MS/s
constexpr size_t kMaxServerClients = 4;

}  // namespace

// Forward declaration for make_default_server.
extern "C" void emuhem_baseband_set_sample_rate(uint32_t hz);

std::unique_ptr<RtlTcpServer> RtlTcpServer::start(const std::string& bind_host, uint16_t port) {
    // Resolve bind address via getaddrinfo so IPv6 works alongside IPv4.
    // Empty or wildcard host binds to AF_UNSPEC with AI_PASSIVE, which
    // picks up whichever family getaddrinfo returns first (usually IPv6
    // with dual-stack enabled).
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    const std::string port_s = std::to_string(port);
    const bool wildcard = bind_host.empty() || bind_host == "*" || bind_host == "0.0.0.0" || bind_host == "::";
    const char* host_arg = wildcard ? nullptr : bind_host.c_str();

    addrinfo* res = nullptr;
    if (const int rc = ::getaddrinfo(host_arg, port_s.c_str(), &hints, &res); rc != 0) {
        std::fprintf(stderr, "[EmuHem] rtl_tcp_server: getaddrinfo(%s:%u) failed: %s\n",
                     wildcard ? "*" : bind_host.c_str(), port, ::gai_strerror(rc));
        return nullptr;
    }
    std::unique_ptr<addrinfo, void (*)(addrinfo*)> res_guard{res, &::freeaddrinfo};

    int fd = -1;
    int last_errno = 0;
    for (auto* p = res; p; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) { last_errno = errno; continue; }
        const int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        // Allow IPv6 sockets to also accept IPv4 connections (dual-stack).
        if (p->ai_family == AF_INET6) {
            const int zero = 0;
            ::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof(zero));
        }
        if (::bind(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        last_errno = errno;
        ::close(fd);
        fd = -1;
    }
    if (fd < 0) {
        std::fprintf(stderr, "[EmuHem] rtl_tcp_server: bind(%s:%u) failed: %s\n",
                     wildcard ? "*" : bind_host.c_str(), port, std::strerror(last_errno));
        return nullptr;
    }
    if (::listen(fd, 4) < 0) {
        std::fprintf(stderr, "[EmuHem] rtl_tcp_server: listen failed: %s\n", std::strerror(errno));
        ::close(fd);
        return nullptr;
    }

    auto srv = std::unique_ptr<RtlTcpServer>(new RtlTcpServer{});
    srv->listen_fd_ = fd;
    srv->bound_port_ = port;
    std::fprintf(stderr, "[EmuHem] rtl_tcp_server: listening on %s:%u\n",
                 bind_host.empty() ? "0.0.0.0" : bind_host.c_str(), port);
    srv->accept_thread_ = std::thread([s = srv.get()] { s->accept_loop(); });
    return srv;
}

RtlTcpServer::~RtlTcpServer() {
    stop_.store(true);
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (accept_thread_.joinable()) accept_thread_.join();

    // Tear down remaining clients.
    std::vector<std::unique_ptr<Client>> pending;
    {
        std::lock_guard<std::mutex> lk(clients_mutex_);
        pending.swap(clients_);
    }
    for (auto& c : pending) {
        c->stop.store(true);
        if (c->fd >= 0) {
            ::shutdown(c->fd, SHUT_RDWR);
            ::close(c->fd);
            c->fd = -1;
        }
        c->cv.notify_all();
        if (c->send_thread.joinable()) c->send_thread.join();
        if (c->recv_thread.joinable()) c->recv_thread.join();
    }
}

void RtlTcpServer::push(const complex8_t* samples, size_t count) {
    if (client_count_.load(std::memory_order_relaxed) == 0) return;

    std::lock_guard<std::mutex> lk(clients_mutex_);
    for (auto& c : clients_) {
        if (c->stop.load()) continue;
        std::lock_guard<std::mutex> clk(c->mutex);
        // cs8 -> cu8 via XOR 0x80; emit 2 bytes per input sample.
        for (size_t i = 0; i < count; ++i) {
            for (int k = 0; k < 2; ++k) {
                const uint8_t b = static_cast<uint8_t>(
                    (k == 0 ? samples[i].real() : samples[i].imag())) ^ 0x80;
                if (c->count >= c->ring.size()) {
                    c->tail = (c->tail + 1) % c->ring.size();
                    --c->count;
                    c->dropped_samples.fetch_add(1, std::memory_order_relaxed);
                }
                c->ring[c->head] = b;
                c->head = (c->head + 1) % c->ring.size();
                ++c->count;
            }
        }
        c->cv.notify_one();
    }
}

void RtlTcpServer::accept_loop() {
    while (!stop_.load()) {
        sockaddr_storage cli{};
        socklen_t cli_len = sizeof(cli);
        const int cfd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&cli), &cli_len);
        if (cfd < 0) {
            if (stop_.load()) break;
            if (errno == EINTR) continue;
            std::fprintf(stderr, "[EmuHem] rtl_tcp_server: accept failed: %s\n", std::strerror(errno));
            break;
        }

        // Reject overload past kMaxServerClients.
        {
            std::lock_guard<std::mutex> lk(clients_mutex_);
            if (clients_.size() >= kMaxServerClients) {
                ::close(cfd);
                continue;
            }
        }

        // Send the 12-byte dongle header: "RTL0" + tuner=5 (R820T) + gains=29.
        uint8_t hdr[12] = {'R', 'T', 'L', '0',
                           0, 0, 0, 5,
                           0, 0, 0, 29};
        if (!write_exact(cfd, hdr, sizeof(hdr))) {
            ::close(cfd);
            continue;
        }

        // TCP_NODELAY reduces latency for small cu8 packets.
        const int one = 1;
        ::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        auto client = std::make_unique<Client>();
        client->fd = cfd;
        client->ring.assign(kServerClientRingBytes, 0);
        auto* raw = client.get();

        {
            std::lock_guard<std::mutex> lk(clients_mutex_);
            clients_.push_back(std::move(client));
        }
        client_count_.fetch_add(1, std::memory_order_relaxed);

        char addr_buf[INET6_ADDRSTRLEN] = {};
        uint16_t cli_port = 0;
        if (cli.ss_family == AF_INET) {
            const auto* a = reinterpret_cast<const sockaddr_in*>(&cli);
            ::inet_ntop(AF_INET, &a->sin_addr, addr_buf, sizeof(addr_buf));
            cli_port = ntohs(a->sin_port);
        } else if (cli.ss_family == AF_INET6) {
            const auto* a = reinterpret_cast<const sockaddr_in6*>(&cli);
            ::inet_ntop(AF_INET6, &a->sin6_addr, addr_buf, sizeof(addr_buf));
            cli_port = ntohs(a->sin6_port);
        }
        std::fprintf(stderr, "[EmuHem] rtl_tcp_server: client connected from %s:%u (fd=%d)\n",
                     addr_buf, cli_port, cfd);

        raw->send_thread = std::thread([this, raw] { client_send_loop(raw); });
        raw->recv_thread = std::thread([this, raw] { client_recv_loop(raw); });
    }
}

void RtlTcpServer::client_send_loop(Client* c) {
    std::vector<uint8_t> scratch(16384);
    while (!c->stop.load() && !stop_.load()) {
        size_t take = 0;
        {
            std::unique_lock<std::mutex> lk(c->mutex);
            c->cv.wait_for(lk, std::chrono::milliseconds(100), [&] {
                return c->count > 0 || c->stop.load() || stop_.load();
            });
            if (c->stop.load() || stop_.load()) break;
            take = std::min(c->count, scratch.size());
            for (size_t i = 0; i < take; ++i) {
                scratch[i] = c->ring[c->tail];
                c->tail = (c->tail + 1) % c->ring.size();
            }
            c->count -= take;
        }
        if (take == 0) continue;
        if (!write_exact(c->fd, scratch.data(), take)) {
            if (!c->stop.load() && !stop_.load()) {
                std::fprintf(stderr, "[EmuHem] rtl_tcp_server: client send failed (fd=%d): %s\n",
                             c->fd, std::strerror(errno));
            }
            c->stop.store(true);
            break;
        }
    }
    // Break recv loop too.
    if (c->fd >= 0) ::shutdown(c->fd, SHUT_RDWR);
}

void RtlTcpServer::client_recv_loop(Client* c) {
    uint8_t pkt[5];
    while (!c->stop.load() && !stop_.load()) {
        if (!read_exact(c->fd, pkt, sizeof(pkt))) {
            c->stop.store(true);
            break;
        }
        const uint8_t cmd = pkt[0];
        const uint32_t param = (uint32_t(pkt[1]) << 24) | (uint32_t(pkt[2]) << 16)
                             | (uint32_t(pkt[3]) << 8)  | uint32_t(pkt[4]);
        switch (cmd) {
            case 0x01:
                std::fprintf(stderr, "[EmuHem] rtl_tcp_server: client set_frequency(%u Hz)\n", param);
                break;
            case 0x02:
                std::fprintf(stderr, "[EmuHem] rtl_tcp_server: client set_sample_rate(%u Hz)\n", param);
                // Apply so the DMA pacing matches what the client expects.
                emuhem_baseband_set_sample_rate(param);
                break;
            case 0x04:
                std::fprintf(stderr, "[EmuHem] rtl_tcp_server: client set_tuner_gain(%d/10 dB)\n",
                             static_cast<int32_t>(param));
                break;
            case 0x03: case 0x05: case 0x06: case 0x07: case 0x08:
            case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d: case 0x0e:
                // Accept silently; most clients send a burst of these at connect time.
                break;
            default:
                std::fprintf(stderr, "[EmuHem] rtl_tcp_server: unknown cmd 0x%02x param=0x%08x\n",
                             cmd, param);
                break;
        }
    }
    client_count_.fetch_sub(1, std::memory_order_relaxed);
    std::fprintf(stderr, "[EmuHem] rtl_tcp_server: client disconnected (fd=%d, dropped=%llu)\n",
                 c->fd, static_cast<unsigned long long>(c->dropped_samples.load()));
}

// ============================================================================
// FrequencyShiftingSource
// ============================================================================

namespace {

// Build a small cos/sin lookup table indexed by the high bits of the 32-bit
// phase accumulator. 1024 entries @ float pair = 8 KB, ample resolution for
// cu8 samples (8-bit output — bin quantization dominates error).
constexpr size_t kNcoTableSize = 1024;
constexpr uint32_t kNcoTableShift = 32 - 10;  // top 10 bits index the table

struct NcoTable {
    std::array<float, kNcoTableSize> cos_tbl{};
    std::array<float, kNcoTableSize> sin_tbl{};
    NcoTable() {
        constexpr double kTwoPi = 6.283185307179586;
        for (size_t i = 0; i < kNcoTableSize; ++i) {
            const double theta = kTwoPi * static_cast<double>(i) / kNcoTableSize;
            cos_tbl[i] = static_cast<float>(std::cos(theta));
            sin_tbl[i] = static_cast<float>(std::sin(theta));
        }
    }
};

const NcoTable& nco_table() {
    static const NcoTable tbl;
    return tbl;
}

}  // namespace

FrequencyShiftingSource::FrequencyShiftingSource(std::unique_ptr<IQSource> inner,
                                                 uint64_t declared_center_hz,
                                                 uint32_t initial_sample_rate)
    : inner_(std::move(inner)), declared_center_hz_(declared_center_hz) {
    sample_rate_.store(initial_sample_rate);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "nco-shift(center=%llu)->%s",
                  static_cast<unsigned long long>(declared_center_hz_),
                  inner_ ? inner_->name() : "(null)");
    display_name_ = buf;
}

size_t FrequencyShiftingSource::read(complex8_t* out, size_t count) {
    const size_t n = inner_ ? inner_->read(out, count) : 0;
    if (n == 0) return 0;

    int32_t inc;
    uint32_t phase;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        inc = phase_inc_;
        phase = phase_;
    }
    if (inc == 0) {
        // No shift active — leave samples untouched.
        return n;
    }

    const auto& tbl = nco_table();
    auto clamp8 = [](float v) -> int8_t {
        if (v > 127.0f) return 127;
        if (v < -127.0f) return -127;
        return static_cast<int8_t>(std::lround(v));
    };

    for (size_t i = 0; i < n; ++i) {
        const uint32_t idx = phase >> kNcoTableShift;
        const float c = tbl.cos_tbl[idx];
        const float s = tbl.sin_tbl[idx];
        const float ri = static_cast<float>(out[i].real());
        const float qi = static_cast<float>(out[i].imag());
        // Complex multiply: (ri + j*qi) * (c + j*s)
        const float ro = ri * c - qi * s;
        const float qo = ri * s + qi * c;
        out[i] = {clamp8(ro), clamp8(qo)};
        phase += static_cast<uint32_t>(inc);
    }

    {
        std::lock_guard<std::mutex> lk(mutex_);
        phase_ = phase;
    }
    return n;
}

void FrequencyShiftingSource::on_sample_rate_changed(uint32_t hz) {
    sample_rate_.store(hz);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        recompute_phase_inc_locked();
    }
    if (inner_) inner_->on_sample_rate_changed(hz);
}

void FrequencyShiftingSource::on_center_frequency_changed(uint64_t hz) {
    tuned_hz_.store(hz);
    tuned_set_.store(true);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        recompute_phase_inc_locked();
    }
    if (inner_) inner_->on_center_frequency_changed(hz);
}

void FrequencyShiftingSource::on_tuner_gain_changed(int32_t tenths_db) {
    if (inner_) inner_->on_tuner_gain_changed(tenths_db);
}

void FrequencyShiftingSource::recompute_phase_inc_locked() {
    const uint32_t fs = sample_rate_.load();
    if (fs == 0 || declared_center_hz_ == 0 || !tuned_set_.load()) {
        phase_inc_ = 0;
        return;
    }
    // shift_hz = declared - tuned (can be negative for tuned > declared).
    const int64_t declared = static_cast<int64_t>(declared_center_hz_);
    const int64_t tuned = static_cast<int64_t>(tuned_hz_.load());
    const int64_t shift_hz = declared - tuned;
    // phase_inc = 2^32 * shift_hz / fs (sign preserved).
    // Use 128-bit-ish promotion via double to avoid overflow in the product.
    const double frac = static_cast<double>(shift_hz) / static_cast<double>(fs);
    // Map frac (can be any real) to a 32-bit signed increment. Very large
    // shift values alias naturally (NCO mod 2^32); we just need a wrap-safe
    // int32 representation.
    const double scaled = frac * 4294967296.0;  // 2^32
    // Normalize into [-2^31, 2^31) via double fmod, then cast.
    double n = std::fmod(scaled, 4294967296.0);
    if (n >= 2147483648.0) n -= 4294967296.0;
    if (n < -2147483648.0) n += 4294967296.0;
    phase_inc_ = static_cast<int32_t>(std::llround(n));
    std::fprintf(stderr,
                 "[EmuHem] nco: declared=%llu tuned=%llu shift=%lld Hz @ %u Hz fs (inc=0x%08x)\n",
                 static_cast<unsigned long long>(declared_center_hz_),
                 static_cast<unsigned long long>(tuned_hz_.load()),
                 static_cast<long long>(shift_hz), fs,
                 static_cast<uint32_t>(phase_inc_));
}

// ============================================================================
// Factory
// ============================================================================

namespace {

std::optional<std::pair<std::string, uint16_t>> parse_host_port(const std::string& s) {
    if (s.empty()) return std::nullopt;

    // Bracketed IPv6: [::1]:1234 → host="::1", port=1234.
    if (s.front() == '[') {
        const auto close = s.find(']');
        if (close == std::string::npos) return std::nullopt;
        if (close + 2 >= s.size() || s[close + 1] != ':') return std::nullopt;
        const std::string host = s.substr(1, close - 1);
        const long port = std::strtol(s.c_str() + close + 2, nullptr, 10);
        if (port <= 0 || port > 65535) return std::nullopt;
        return std::make_pair(host, static_cast<uint16_t>(port));
    }

    const auto colon = s.rfind(':');
    if (colon == std::string::npos || colon + 1 >= s.size()) return std::nullopt;
    // Empty host (":1234") is valid — bind to INADDR_ANY.
    const std::string host = s.substr(0, colon);
    const long port = std::strtol(s.c_str() + colon + 1, nullptr, 10);
    if (port <= 0 || port > 65535) return std::nullopt;
    return std::make_pair(host, static_cast<uint16_t>(port));
}

}  // namespace

std::unique_ptr<IQSource> make_default_source() {
    std::unique_ptr<IQSource> base;
    bool is_network_tuned = false;  // network sources tune their own dongle
    if (const char* path = std::getenv("EMUHEM_IQ_FILE"); path && *path) {
        const char* loop_env = std::getenv("EMUHEM_IQ_LOOP");
        const bool loop = !loop_env || std::string{loop_env} != "0";
        if (auto fs = FileIQSource::open(path, loop)) {
            base = std::move(fs);
        } else {
            std::fprintf(stderr, "[EmuHem] iq_source: falling back after file open failure\n");
        }
    }
    if (!base) {
        if (const char* tcp = std::getenv("EMUHEM_IQ_TCP"); tcp && *tcp) {
            if (auto hp = parse_host_port(tcp)) {
                if (auto src = RtlTcpClientSource::open(hp->first, hp->second)) {
                    base = std::move(src);
                    is_network_tuned = true;
                } else {
                    std::fprintf(stderr, "[EmuHem] iq_source: falling back after rtl_tcp connect failure\n");
                }
            } else {
                std::fprintf(stderr, "[EmuHem] iq_source: EMUHEM_IQ_TCP must be host:port (got '%s')\n", tcp);
            }
        }
    }
    if (!base) {
        base = std::make_unique<NoiseIQSource>();
    }

    if (!is_network_tuned) {
        if (const char* center = std::getenv("EMUHEM_IQ_CENTER"); center && *center) {
            const uint64_t hz = std::strtoull(center, nullptr, 10);
            if (hz > 0) {
                std::fprintf(stderr, "[EmuHem] iq_source: NCO shift active, declared center = %llu Hz\n",
                             static_cast<unsigned long long>(hz));
                return std::make_unique<FrequencyShiftingSource>(std::move(base), hz);
            }
        }
    }
    return base;
}

std::unique_ptr<RtlTcpServer> make_default_server() {
    const char* spec = std::getenv("EMUHEM_RTL_TCP_SERVER");
    if (!spec || !*spec) return nullptr;
    auto hp = parse_host_port(spec);
    if (!hp) {
        std::fprintf(stderr, "[EmuHem] rtl_tcp_server: EMUHEM_RTL_TCP_SERVER must be host:port (got '%s')\n", spec);
        return nullptr;
    }
    return RtlTcpServer::start(hp->first, hp->second);
}

}  // namespace emuhem
