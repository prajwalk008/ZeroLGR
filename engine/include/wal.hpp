// engine/include/wal.hpp
// Memory-mapped Write-Ahead Log for ZeroLGR.
//
// Design:
// - Pre-allocates a large file (default 64MB) and maps it entirely into
//   virtual memory using mmap (Linux) or CreateFileMapping (Windows).
// - Frames are written sequentially as: [magic(4B)][size(4B)][data(NB)][crc32(4B)]
// - No page-boundary padding per frame — we track a plain byte offset.
// - msync(MS_SYNC) is called after each append to guarantee durability
//   before the engine returns an ACK to the gateway. This is the ACID
//   guarantee: if the process crashes after append() returns, the frame
//   is on disk.
// - On startup, replay() scans from offset 0, validates magic + CRC32 on
//   each frame, and stops at the first corrupt/incomplete frame (torn write).
//
// Cross-platform: Abstracts mmap (POSIX) and MapViewOfFile (Windows)
// behind a unified interface.

#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <span>
#include <string>

namespace zerolgr {

class WriteAheadLog {
public:
    // Opens (or creates) the WAL file at `path`.
    // Maps `initial_size` bytes into virtual memory.
    // `initial_size` must be a multiple of the OS page size (auto-rounded up).
    explicit WriteAheadLog(const std::string& path,
                           std::size_t initial_size = 64 * 1024 * 1024);

    ~WriteAheadLog();

    // Non-copyable, non-moveable (owns OS file handles and memory maps)
    WriteAheadLog(const WriteAheadLog&) = delete;
    WriteAheadLog& operator=(const WriteAheadLog&) = delete;
    WriteAheadLog(WriteAheadLog&&) = delete;
    WriteAheadLog& operator=(WriteAheadLog&&) = delete;

    // ── Write Operations ──────────────────────────────────────────────────

    // Appends a serialized data frame to the WAL.
    // Frame format: [magic(4B)][size(4B)][data][crc32(4B)]
    // Calls platform sync (msync/FlushViewOfFile) before returning.
    // Returns the monotonically increasing sequence number for this frame.
    // Throws std::runtime_error if the WAL is full (cannot grow).
    uint64_t append(std::span<const uint8_t> data);

    // Hint to the OS to flush dirty pages asynchronously (non-blocking).
    void flush_async();

    // ── Read Operations ───────────────────────────────────────────────────

    // Replays all valid frames from the WAL file.
    // Calls `callback(sequence_number, data_span)` for each valid frame.
    // Stops at the first frame that fails magic or CRC32 validation.
    // Returns the number of frames successfully replayed.
    uint64_t replay(std::function<void(uint64_t seq,
                                       std::span<const uint8_t> data)> callback);

    // ── Accessors ─────────────────────────────────────────────────────────

    [[nodiscard]] uint64_t    current_sequence() const { return sequence_; }
    [[nodiscard]] std::size_t write_offset()     const { return write_offset_; }
    [[nodiscard]] std::size_t mapped_size()      const { return mapped_size_; }
    [[nodiscard]] std::size_t bytes_remaining()  const {
        return mapped_size_ > write_offset_ ? mapped_size_ - write_offset_ : 0;
    }

private:
    // ── Platform Abstraction ──────────────────────────────────────────────

    void open_and_map(const std::string& path, std::size_t size);
    void unmap_and_close();
    void sync_range(std::size_t offset, std::size_t length, bool blocking);
    void grow(std::size_t new_size);

    static std::size_t page_size();
    static std::size_t align_to_page(std::size_t size);

    // ── CRC32 ─────────────────────────────────────────────────────────────

    static uint32_t compute_crc32(const uint8_t* data, std::size_t length);

    // ── State ─────────────────────────────────────────────────────────────

    uint8_t*    mapped_ptr_{nullptr};
    std::size_t mapped_size_{0};
    std::size_t write_offset_{0};
    uint64_t    sequence_{0};
    std::string path_;

#ifdef _WIN32
    void*  file_handle_{nullptr};   // HANDLE from CreateFileW
    void*  map_handle_{nullptr};    // HANDLE from CreateFileMappingW
#else
    int    fd_{-1};                 // POSIX file descriptor
#endif
};

}  // namespace zerolgr
