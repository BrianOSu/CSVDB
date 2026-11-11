#define KXVER 3
#include "k.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <fcntl.h>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace csvdb {

// RAII wrapper for POSIX file descriptors
class FileDescriptor {
    int fd_ = -1;

public:
    explicit FileDescriptor(int fd) noexcept : fd_(fd) {}

    ~FileDescriptor() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    FileDescriptor(FileDescriptor&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) {
                ::close(fd_);
            }
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool is_valid() const noexcept { return fd_ >= 0; }
};

// RAII wrapper for memory-mapped file with graceful heap-read fallback
class MemoryMappedFile {
    void* addr_ = MAP_FAILED;
    size_t size_ = 0;
    std::vector<char> fallback_buffer_;
    bool is_mmap_ = false;

public:
    MemoryMappedFile() = default;

    static std::expected<MemoryMappedFile, const char*> open_and_map(const char* path) {
        if (!path || !*path) {
            return std::unexpected("invalid file path");
        }

        int fd = ::open(path, O_RDONLY);
        if (fd < 0) {
            return std::unexpected("cannot open file");
        }
        FileDescriptor fd_guard(fd);

        struct stat st{};
        if (::fstat(fd, &st) < 0) {
            return std::unexpected("cannot stat file");
        }

        MemoryMappedFile mmf;
        if (st.st_size < 0) {
            return std::unexpected("invalid file size");
        }
        mmf.size_ = static_cast<size_t>(st.st_size);

        if (mmf.size_ == 0) {
            return mmf;
        }

        void* mapped = ::mmap(nullptr, mmf.size_, PROT_READ, MAP_SHARED, fd, 0);
        if (mapped != MAP_FAILED) {
            mmf.addr_ = mapped;
            mmf.is_mmap_ = true;
        } else {
            try {
                mmf.fallback_buffer_.resize(mmf.size_);
            } catch (const std::bad_alloc&) {
                return std::unexpected("out of memory");
            }

            ssize_t bytes_read = ::read(fd, mmf.fallback_buffer_.data(), mmf.size_);
            if (bytes_read < 0) {
                return std::unexpected("read error");
            }
            mmf.size_ = static_cast<size_t>(bytes_read);
            mmf.fallback_buffer_.resize(mmf.size_);
            mmf.addr_ = mmf.fallback_buffer_.data();
            mmf.is_mmap_ = false;
        }

        return mmf;
    }

    ~MemoryMappedFile() noexcept {
        if (is_mmap_ && addr_ != MAP_FAILED && addr_ != nullptr) {
            ::munmap(addr_, size_);
        }
    }

    MemoryMappedFile(const MemoryMappedFile&) = delete;
    MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;

    MemoryMappedFile(MemoryMappedFile&& other) noexcept
        : addr_(other.addr_), size_(other.size_),
          fallback_buffer_(std::move(other.fallback_buffer_)), is_mmap_(other.is_mmap_) {
        other.addr_ = MAP_FAILED;
        other.size_ = 0;
        other.is_mmap_ = false;
    }

    MemoryMappedFile& operator=(MemoryMappedFile&& other) noexcept {
        if (this != &other) {
            if (is_mmap_ && addr_ != MAP_FAILED && addr_ != nullptr) {
                ::munmap(addr_, size_);
            }
            addr_ = other.addr_;
            size_ = other.size_;
            fallback_buffer_ = std::move(other.fallback_buffer_);
            is_mmap_ = other.is_mmap_;
            other.addr_ = MAP_FAILED;
            other.size_ = 0;
            other.is_mmap_ = false;
        }
        return *this;
    }

    [[nodiscard]] std::string_view view() const noexcept {
        if (size_ == 0 || addr_ == MAP_FAILED || addr_ == nullptr) {
            return {};
        }
        return std::string_view(static_cast<const char*>(addr_), size_);
    }

    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
};

// Safe string trimming for whitespace and optional enclosing quotes
constexpr std::string_view trim_whitespace_and_quotes(std::string_view sv) noexcept {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
        sv.remove_suffix(1);
    }
    if (sv.size() >= 2 && sv.front() == '"' && sv.back() == '"') {
        sv.remove_prefix(1);
        sv.remove_suffix(1);
    }
    return sv;
}

// Case-insensitive ASCII comparison
constexpr bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// Map single character type indicator to KDB type code
constexpr int get_ktype_from_char(char c) noexcept {
    switch (c) {
        case 'b': case 'B': return KB;
        case 'g': case 'G': case 'x': case 'X': return KG;
        case 'h': case 'H': return KH;
        case 'i': case 'I': return KI;
        case 'j': case 'J': return KJ;
        case 'e': case 'E': return KE;
        case 'f': case 'F': return KF;
        case 'c': case 'C': return KC;
        case 's': case 'S': return KS;
        case 'p': case 'P': return KP;
        case 'm': case 'M': return KM;
        case 'd': case 'D': return KD;
        case 'z': case 'Z': return KZ;
        case 'n': case 'N': return KN;
        case 'u': case 'U': return KU;
        case 'v': case 'V': return KV;
        case 't': case 'T': return KT;
        default: return -1;
    }
}

// Map string type descriptor to KDB type code
constexpr int get_ktype_from_str(std::string_view s) noexcept {
    if (s.size() == 1) return get_ktype_from_char(s.front());
    if (s.empty()) return -1;

    if (iequals(s, "float") || iequals(s, "real8") || iequals(s, "double")) return KF;
    if (iequals(s, "real") || iequals(s, "real4")) return KE;
    if (iequals(s, "long") || iequals(s, "int8")) return KJ;
    if (iequals(s, "int") || iequals(s, "int4")) return KI;
    if (iequals(s, "short") || iequals(s, "int2")) return KH;
    if (iequals(s, "byte") || iequals(s, "char1")) return KG;
    if (iequals(s, "boolean") || iequals(s, "bool")) return KB;
    if (iequals(s, "char") || iequals(s, "string")) return KC;
    if (iequals(s, "sym") || iequals(s, "symbol")) return KS;
    if (iequals(s, "time")) return KT;
    if (iequals(s, "date")) return KD;
    if (iequals(s, "datetime")) return KZ;
    if (iequals(s, "timestamp")) return KP;
    if (iequals(s, "timespan")) return KN;
    if (iequals(s, "minute")) return KU;
    if (iequals(s, "second")) return KV;
    if (iequals(s, "month")) return KM;
    return -1;
}

// Extract KDB type code from K object (char atom, string vector, symbol atom, or numeric code)
int get_ktype(K typeK) noexcept {
    if (!typeK) return -1;
    if (typeK->t == -KC) {
        return get_ktype_from_char(static_cast<char>(typeK->g));
    }
    if (typeK->t == KC) {
        return get_ktype_from_str(std::string_view(reinterpret_cast<const char*>(kC(typeK)), static_cast<size_t>(typeK->n)));
    }
    if (typeK->t == -KS) {
        const char* s = typeK->s;
        if (!s || !*s) return -1;
        return get_ktype_from_str(std::string_view(s));
    }
    if (typeK->t == -KH || typeK->t == -KI || typeK->t == -KJ) {
        int val = (typeK->t == -KH) ? typeK->h : (typeK->t == -KI ? typeK->i : static_cast<int>(typeK->j));
        if (val < 0) val = -val;
        if (val >= KB && val <= KT) return val;
    }
    return -1;
}

// Extract file path from K object safely
std::expected<std::string, const char*> extract_filepath(K pathK) {
    if (!pathK) {
        return std::unexpected("null path parameter");
    }

    std::string_view src;
    if (pathK->t == -KS) {
        if (!pathK->s) return std::unexpected("null symbol path");
        src = pathK->s;
    } else if (pathK->t == KC) {
        src = std::string_view(reinterpret_cast<const char*>(kC(pathK)), static_cast<size_t>(pathK->n));
    } else if (pathK->t == -KC) {
        src = std::string_view(reinterpret_cast<const char*>(&pathK->g), 1);
    } else {
        return std::unexpected("invalid file path type");
    }

    if (!src.empty() && src.front() == ':') {
        src.remove_prefix(1);
    }

    if (src.empty()) {
        return std::unexpected("empty file path");
    }

    return std::string(src);
}

// Fast temporal parsing helper functions
template <typename T>
bool parse_int_prefix(std::string_view& sv, T& out) noexcept {
    if (sv.empty()) return false;
    const char* start = sv.data();
    const char* end = sv.data() + sv.size();
    auto [ptr, ec] = std::from_chars(start, end, out);
    if (ec != std::errc() || ptr == start) return false;
    sv.remove_prefix(static_cast<size_t>(ptr - start));
    return true;
}

int parse_fractional_ms(std::string_view sv) noexcept {
    int ms = 0;
    int mult = 100;
    for (char c : sv) {
        if (c >= '0' && c <= '9') {
            if (mult > 0) {
                ms += (c - '0') * mult;
                mult /= 10;
            }
        } else {
            break;
        }
    }
    return ms;
}

long long parse_fractional_nanos(std::string_view sv) noexcept {
    long long nanos = 0;
    long long mult = 100000000LL;
    for (char c : sv) {
        if (c >= '0' && c <= '9') {
            if (mult > 0) {
                nanos += static_cast<long long>(c - '0') * mult;
                mult /= 10;
            }
        } else {
            break;
        }
    }
    return nanos;
}

// Store a typed null value at out_idx in the result vector
void store_null(int ktype, K res, J out_idx) noexcept {
    switch (ktype) {
        case KS: kS(res)[out_idx] = ss(const_cast<S>("")); break;
        case KF: kF(res)[out_idx] = nf; break;
        case KE: kE(res)[out_idx] = static_cast<E>(nf); break;
        case KJ: kJ(res)[out_idx] = nj; break;
        case KI: kI(res)[out_idx] = ni; break;
        case KH: kH(res)[out_idx] = static_cast<H>(nh); break;
        case KB: kG(res)[out_idx] = 0; break;
        case KG: kG(res)[out_idx] = 0; break;
        case KC: kC(res)[out_idx] = ' '; break;
        case KT: case KD: case KM: case KU: case KV: kI(res)[out_idx] = ni; break;
        case KN: case KP: kJ(res)[out_idx] = nj; break;
        case KZ: kF(res)[out_idx] = nf; break;
        default: break;
    }
}

// Parse a single text line into target KDB type and store at out_idx
void parse_and_store(int ktype, std::string_view raw_sv, K res, J out_idx) noexcept {
    std::string_view sv = trim_whitespace_and_quotes(raw_sv);

    switch (ktype) {
        case KS: { // Symbol
            if (sv.empty()) {
                kS(res)[out_idx] = ss(const_cast<S>(""));
            } else {
                kS(res)[out_idx] = sn(const_cast<S>(sv.data()), static_cast<I>(sv.size()));
            }
            break;
        }
        case KF: { // Float (double)
            if (sv.empty() || iequals(sv, "0n")) {
                kF(res)[out_idx] = nf;
            } else {
                double val = 0.0;
                auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
                kF(res)[out_idx] = (ec == std::errc() && ptr != sv.data()) ? val : nf;
            }
            break;
        }
        case KE: { // Real (float 32)
            if (sv.empty() || iequals(sv, "0n")) {
                kE(res)[out_idx] = static_cast<E>(nf);
            } else {
                float val = 0.0f;
                auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
                kE(res)[out_idx] = (ec == std::errc() && ptr != sv.data()) ? val : static_cast<E>(nf);
            }
            break;
        }
        case KJ: { // Long
            if (sv.empty() || iequals(sv, "0N")) {
                kJ(res)[out_idx] = nj;
            } else {
                long long val = 0;
                auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val, 10);
                kJ(res)[out_idx] = (ec == std::errc() && ptr != sv.data()) ? static_cast<J>(val) : nj;
            }
            break;
        }
        case KI: { // Int
            if (sv.empty() || iequals(sv, "0N")) {
                kI(res)[out_idx] = ni;
            } else {
                int val = 0;
                auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val, 10);
                kI(res)[out_idx] = (ec == std::errc() && ptr != sv.data()) ? static_cast<I>(val) : ni;
            }
            break;
        }
        case KH: { // Short
            if (sv.empty() || iequals(sv, "0N")) {
                kH(res)[out_idx] = static_cast<H>(nh);
            } else {
                short val = 0;
                auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val, 10);
                kH(res)[out_idx] = (ec == std::errc() && ptr != sv.data()) ? static_cast<H>(val) : static_cast<H>(nh);
            }
            break;
        }
        case KB: { // Boolean
            if (sv.empty()) {
                kG(res)[out_idx] = 0;
            } else if (sv.front() == '1' || sv.front() == 't' || sv.front() == 'T') {
                kG(res)[out_idx] = 1;
            } else {
                kG(res)[out_idx] = 0;
            }
            break;
        }
        case KG: { // Byte
            if (sv.empty()) {
                kG(res)[out_idx] = 0;
            } else if (sv.size() >= 2 && sv[0] == '0' && (sv[1] == 'x' || sv[1] == 'X')) {
                uint8_t val = 0;
                auto [ptr, ec] = std::from_chars(sv.data() + 2, sv.data() + sv.size(), val, 16);
                kG(res)[out_idx] = (ec == std::errc()) ? static_cast<G>(val) : 0;
            } else {
                uint8_t val = 0;
                auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val, 10);
                kG(res)[out_idx] = (ec == std::errc()) ? static_cast<G>(val) : 0;
            }
            break;
        }
        case KC: { // Char
            kC(res)[out_idx] = !sv.empty() ? sv.front() : ' ';
            break;
        }
        case KT: { // Time (HH:MM:SS.mmm) -> ms from midnight
            if (sv.empty() || iequals(sv, "0N")) {
                kI(res)[out_idx] = ni;
            } else {
                int hh = 0, mm = 0, ss = 0;
                std::string_view rem = sv;
                parse_int_prefix(rem, hh);
                if (!rem.empty() && rem.front() == ':') rem.remove_prefix(1);
                parse_int_prefix(rem, mm);
                if (!rem.empty() && rem.front() == ':') rem.remove_prefix(1);
                parse_int_prefix(rem, ss);

                int ms = 0;
                if (!rem.empty() && rem.front() == '.') {
                    rem.remove_prefix(1);
                    ms = parse_fractional_ms(rem);
                }
                kI(res)[out_idx] = static_cast<I>((hh * 3600 + mm * 60 + ss) * 1000 + ms);
            }
            break;
        }
        case KD: { // Date (YYYY.MM.DD) -> days from 2000.01.01
            if (sv.empty() || iequals(sv, "0N")) {
                kI(res)[out_idx] = ni;
            } else {
                int y = 0, m = 0, d = 0;
                std::string_view rem = sv;
                parse_int_prefix(rem, y);
                if (!rem.empty() && (rem.front() == '.' || rem.front() == '-')) rem.remove_prefix(1);
                parse_int_prefix(rem, m);
                if (!rem.empty() && (rem.front() == '.' || rem.front() == '-')) rem.remove_prefix(1);
                parse_int_prefix(rem, d);
                kI(res)[out_idx] = static_cast<I>(ymd(y, m, d));
            }
            break;
        }
        case KM: { // Month (YYYY.MM) -> months from 2000.01
            if (sv.empty() || iequals(sv, "0N")) {
                kI(res)[out_idx] = ni;
            } else {
                int y = 0, m = 0;
                std::string_view rem = sv;
                parse_int_prefix(rem, y);
                if (!rem.empty() && (rem.front() == '.' || rem.front() == '-')) rem.remove_prefix(1);
                parse_int_prefix(rem, m);
                kI(res)[out_idx] = static_cast<I>((y - 2000) * 12 + (m - 1));
            }
            break;
        }
        case KU: { // Minute (HH:MM) -> minutes from midnight
            if (sv.empty() || iequals(sv, "0N")) {
                kI(res)[out_idx] = ni;
            } else {
                int hh = 0, mm = 0;
                std::string_view rem = sv;
                parse_int_prefix(rem, hh);
                if (!rem.empty() && rem.front() == ':') rem.remove_prefix(1);
                parse_int_prefix(rem, mm);
                kI(res)[out_idx] = static_cast<I>(hh * 60 + mm);
            }
            break;
        }
        case KV: { // Second (HH:MM:SS) -> seconds from midnight
            if (sv.empty() || iequals(sv, "0N")) {
                kI(res)[out_idx] = ni;
            } else {
                int hh = 0, mm = 0, ss = 0;
                std::string_view rem = sv;
                parse_int_prefix(rem, hh);
                if (!rem.empty() && rem.front() == ':') rem.remove_prefix(1);
                parse_int_prefix(rem, mm);
                if (!rem.empty() && rem.front() == ':') rem.remove_prefix(1);
                parse_int_prefix(rem, ss);
                kI(res)[out_idx] = static_cast<I>(hh * 3600 + mm * 60 + ss);
            }
            break;
        }
        case KN: { // Timespan -> nanoseconds
            if (sv.empty() || iequals(sv, "0N")) {
                kJ(res)[out_idx] = nj;
            } else {
                long long days = 0;
                int hh = 0, mm = 0, ss = 0;
                long long nanos = 0;

                std::string_view rem = sv;
                auto d_pos = rem.find_first_of("Dd");
                if (d_pos != std::string_view::npos) {
                    std::string_view day_sv = rem.substr(0, d_pos);
                    parse_int_prefix(day_sv, days);
                    rem.remove_prefix(d_pos + 1);
                }

                parse_int_prefix(rem, hh);
                if (!rem.empty() && rem.front() == ':') rem.remove_prefix(1);
                parse_int_prefix(rem, mm);
                if (!rem.empty() && rem.front() == ':') rem.remove_prefix(1);
                parse_int_prefix(rem, ss);

                if (!rem.empty() && rem.front() == '.') {
                    rem.remove_prefix(1);
                    nanos = parse_fractional_nanos(rem);
                }

                kJ(res)[out_idx] = static_cast<J>(days * 86400000000000LL +
                                                  (static_cast<long long>(hh) * 3600 + static_cast<long long>(mm) * 60 + ss) * 1000000000LL +
                                                  nanos);
            }
            break;
        }
        case KP: { // Timestamp -> nanoseconds from 2000.01.01
            if (sv.empty() || iequals(sv, "0N")) {
                kJ(res)[out_idx] = nj;
            } else {
                int y = 0, m = 0, d = 0;
                int hh = 0, mm = 0, ss = 0;
                long long nanos = 0;

                std::string_view rem = sv;
                parse_int_prefix(rem, y);
                if (!rem.empty() && (rem.front() == '.' || rem.front() == '-')) rem.remove_prefix(1);
                parse_int_prefix(rem, m);
                if (!rem.empty() && (rem.front() == '.' || rem.front() == '-')) rem.remove_prefix(1);
                parse_int_prefix(rem, d);

                if (!rem.empty() && (rem.front() == 'D' || rem.front() == 'T' || rem.front() == ' ' || rem.front() == 'd' || rem.front() == 't')) {
                    rem.remove_prefix(1);
                }

                parse_int_prefix(rem, hh);
                if (!rem.empty() && rem.front() == ':') rem.remove_prefix(1);
                parse_int_prefix(rem, mm);
                if (!rem.empty() && rem.front() == ':') rem.remove_prefix(1);
                parse_int_prefix(rem, ss);

                if (!rem.empty() && rem.front() == '.') {
                    rem.remove_prefix(1);
                    nanos = parse_fractional_nanos(rem);
                }

                long long days = ymd(y, m, d);
                kJ(res)[out_idx] = static_cast<J>(days * 86400000000000LL +
                                                  (static_cast<long long>(hh) * 3600 + static_cast<long long>(mm) * 60 + ss) * 1000000000LL +
                                                  nanos);
            }
            break;
        }
        case KZ: { // Datetime -> days from 2000.01.01 (double)
            if (sv.empty() || iequals(sv, "0n")) {
                kF(res)[out_idx] = nf;
            } else {
                int y = 0, m = 0, d = 0;
                int hh = 0, mm = 0, ss = 0, ms = 0;

                std::string_view rem = sv;
                parse_int_prefix(rem, y);
                if (!rem.empty() && (rem.front() == '.' || rem.front() == '-')) rem.remove_prefix(1);
                parse_int_prefix(rem, m);
                if (!rem.empty() && (rem.front() == '.' || rem.front() == '-')) rem.remove_prefix(1);
                parse_int_prefix(rem, d);

                if (!rem.empty() && (rem.front() == 'T' || rem.front() == 't' || rem.front() == ' ')) {
                    rem.remove_prefix(1);
                }

                parse_int_prefix(rem, hh);
                if (!rem.empty() && rem.front() == ':') rem.remove_prefix(1);
                parse_int_prefix(rem, mm);
                if (!rem.empty() && rem.front() == ':') rem.remove_prefix(1);
                parse_int_prefix(rem, ss);

                if (!rem.empty() && rem.front() == '.') {
                    rem.remove_prefix(1);
                    ms = parse_fractional_ms(rem);
                }

                int days = ymd(y, m, d);
                double fraction = (hh * 3600 + mm * 60 + ss + ms / 1000.0) / 86400.0;
                kF(res)[out_idx] = static_cast<F>(days + fraction);
            }
            break;
        }
        default:
            break;
    }
}

// Scans line views directly within the memory-mapped content
std::vector<std::string_view> scan_lines(std::string_view content) {
    std::vector<std::string_view> lines;
    lines.reserve(4096);

    const char* data = content.data();
    const size_t total_size = content.size();
    size_t cur = 0;

    while (cur < total_size) {
        size_t line_start = cur;
        while (cur < total_size && data[cur] != '\n' && data[cur] != '\r') {
            cur++;
        }
        size_t line_len = cur - line_start;
        lines.emplace_back(data + line_start, line_len);

        if (cur < total_size && data[cur] == '\r') cur++;
        if (cur < total_size && data[cur] == '\n') cur++;
    }

    return lines;
}

// Core implementation with full exception safety and resource management
K read_csv_impl(K filePath, K typeK, K indicesK) {
    auto path_res = extract_filepath(filePath);
    if (!path_res) {
        return krr(const_cast<S>(path_res.error()));
    }

    int ktype = get_ktype(typeK);
    if (ktype < 0) {
        return krr(const_cast<S>("invalid or unsupported type"));
    }

    auto map_res = MemoryMappedFile::open_and_map(path_res->c_str());
    if (!map_res) {
        return krr(const_cast<S>(map_res.error()));
    }

    const MemoryMappedFile& mmf = *map_res;
    if (mmf.empty()) {
        return ktn(ktype, 0);
    }

    std::vector<std::string_view> lines = scan_lines(mmf.view());

    // Line 0 is header, lines 1..n-1 are data rows
    J total_data_rows = (lines.size() > 1) ? static_cast<J>(lines.size() - 1) : 0;

    bool all_rows = false;
    J out_count = 0;

    if (!indicesK || indicesK->t == 101) { // Generic null (::) -> select all
        all_rows = true;
        out_count = total_data_rows;
    } else if (indicesK->t < 0) { // Atom index
        out_count = 1;
    } else if (indicesK->t >= 0) { // Vector of indices
        out_count = indicesK->n;
    }

    K res = ktn(ktype, out_count);

    if (all_rows) {
        for (J i = 0; i < out_count; ++i) {
            size_t l_idx = static_cast<size_t>(i + 1);
            parse_and_store(ktype, lines[l_idx], res, i);
        }
    } else if (indicesK->t < 0) { // Atom index
        J idx = (indicesK->t == -KJ) ? indicesK->j :
                ((indicesK->t == -KI) ? indicesK->i :
                ((indicesK->t == -KH) ? indicesK->h : -1));
        if (idx >= 0 && idx < total_data_rows) {
            size_t l_idx = static_cast<size_t>(idx + 1);
            parse_and_store(ktype, lines[l_idx], res, 0);
        } else {
            store_null(ktype, res, 0);
        }
    } else { // Vector or generic list of indices
        for (J i = 0; i < out_count; ++i) {
            J idx = -1;
            if (indicesK->t == KJ) {
                idx = kJ(indicesK)[i];
            } else if (indicesK->t == KI) {
                idx = kI(indicesK)[i];
            } else if (indicesK->t == KH) {
                idx = kH(indicesK)[i];
            } else if (indicesK->t == 0) { // Generic list of atoms
                K item = kK(indicesK)[i];
                if (item) {
                    if (item->t == -KJ) idx = item->j;
                    else if (item->t == -KI) idx = item->i;
                    else if (item->t == -KH) idx = item->h;
                }
            }

            if (idx >= 0 && idx < total_data_rows) {
                size_t l_idx = static_cast<size_t>(idx + 1);
                parse_and_store(ktype, lines[l_idx], res, i);
            } else {
                store_null(ktype, res, i);
            }
        }
    }

    return res;
}

} // namespace csvdb

// Exported C-ABI functions with exception catch boundaries
extern "C" {

    K read_csv(K filePath, K typeK, K indicesK) {
        try {
            return csvdb::read_csv_impl(filePath, typeK, indicesK);
        } catch (const std::bad_alloc&) {
            return krr(const_cast<S>("out of memory"));
        } catch (const std::exception& e) {
            return krr(const_cast<S>(e.what()));
        } catch (...) {
            return krr(const_cast<S>("unknown error"));
        }
    }

}
