// engine/src/wal.cpp
// Memory-mapped Write-Ahead Log implementation.
//
// Cross-platform: uses mmap/msync on POSIX, CreateFileMapping/FlushViewOfFile
// on Windows. The frame format is platform-independent.

#include "wal.hpp"
#include "types.hpp"   // WAL_MAGIC

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

namespace zerolgr {

// ── CRC32 (IEEE 802.3 polynomial, lookup-table implementation) ────────────
//
// We compute our own CRC32 to avoid pulling in a heavy external dependency.
// This uses the standard 0xEDB88320 reflected polynomial. The table is
// generated at program startup.

static uint32_t crc32_table[256];
static bool crc32_table_initialized = false;

static void init_crc32_table() {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320u;
            else
                crc >>= 1;
        }
        crc32_table[i] = crc;
    }
    crc32_table_initialized = true;
}

uint32_t WriteAheadLog::compute_crc32(const uint8_t* data, std::size_t length) {
    if (!crc32_table_initialized) init_crc32_table();

    uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < length; ++i) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

// ── Page Size Utilities ───────────────────────────────────────────────────

std::size_t WriteAheadLog::page_size() {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return static_cast<std::size_t>(si.dwAllocationGranularity);
#else
    return static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
#endif
}

std::size_t WriteAheadLog::align_to_page(std::size_t size) {
    const std::size_t ps = page_size();
    return (size + ps - 1) & ~(ps - 1);
}

// ── Constructor / Destructor ──────────────────────────────────────────────

WriteAheadLog::WriteAheadLog(const std::string& path, std::size_t initial_size)
    : path_(path)
{
    const std::size_t aligned_size = align_to_page(
        std::max(initial_size, page_size())
    );
    open_and_map(path, aligned_size);
}

WriteAheadLog::~WriteAheadLog() {
    unmap_and_close();
}

// ── Platform: Open, Map, Unmap, Close ─────────────────────────────────────

#ifdef _WIN32

void WriteAheadLog::open_and_map(const std::string& path, std::size_t size) {
    // Convert narrow path to wide string for CreateFileW
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wpath(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);

    file_handle_ = CreateFileW(
        wpath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (file_handle_ == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("WriteAheadLog: CreateFileW failed for " + path);
    }

    // Set file size
    LARGE_INTEGER li;
    li.QuadPart = static_cast<LONGLONG>(size);
    if (!SetFilePointerEx(file_handle_, li, nullptr, FILE_BEGIN) ||
        !SetEndOfFile(file_handle_)) {
        CloseHandle(file_handle_);
        file_handle_ = nullptr;
        throw std::runtime_error("WriteAheadLog: SetEndOfFile failed");
    }

    // Create file mapping
    map_handle_ = CreateFileMappingW(
        file_handle_,
        nullptr,
        PAGE_READWRITE,
        static_cast<DWORD>(size >> 32),
        static_cast<DWORD>(size & 0xFFFFFFFF),
        nullptr
    );
    if (!map_handle_) {
        CloseHandle(file_handle_);
        file_handle_ = nullptr;
        throw std::runtime_error("WriteAheadLog: CreateFileMappingW failed");
    }

    // Map the view
    mapped_ptr_ = static_cast<uint8_t*>(
        MapViewOfFile(map_handle_, FILE_MAP_ALL_ACCESS, 0, 0, size)
    );
    if (!mapped_ptr_) {
        CloseHandle(map_handle_);
        CloseHandle(file_handle_);
        map_handle_ = nullptr;
        file_handle_ = nullptr;
        throw std::runtime_error("WriteAheadLog: MapViewOfFile failed");
    }

    mapped_size_ = size;
}

void WriteAheadLog::unmap_and_close() {
    if (mapped_ptr_) {
        FlushViewOfFile(mapped_ptr_, mapped_size_);
        UnmapViewOfFile(mapped_ptr_);
        mapped_ptr_ = nullptr;
    }
    if (map_handle_) {
        CloseHandle(map_handle_);
        map_handle_ = nullptr;
    }
    if (file_handle_) {
        CloseHandle(file_handle_);
        file_handle_ = nullptr;
    }
}

void WriteAheadLog::sync_range(std::size_t offset, std::size_t length, bool blocking) {
    if (!mapped_ptr_) return;
    FlushViewOfFile(mapped_ptr_ + offset, length);
    if (blocking) {
        // On Windows, FlushViewOfFile writes to the filesystem cache.
        // FlushFileBuffers forces it to the physical disk.
        FlushFileBuffers(file_handle_);
    }
}

void WriteAheadLog::grow(std::size_t new_size) {
    new_size = align_to_page(new_size);
    unmap_and_close();
    open_and_map(path_, new_size);
}

#else  // POSIX

void WriteAheadLog::open_and_map(const std::string& path, std::size_t size) {
    fd_ = open(path.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd_ < 0) {
        throw std::runtime_error("WriteAheadLog: open() failed for " + path);
    }

    // Extend file to target size
    if (ftruncate(fd_, static_cast<off_t>(size)) != 0) {
        close(fd_);
        fd_ = -1;
        throw std::runtime_error("WriteAheadLog: ftruncate() failed");
    }

    mapped_ptr_ = static_cast<uint8_t*>(
        mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0)
    );
    if (mapped_ptr_ == MAP_FAILED) {
        mapped_ptr_ = nullptr;
        close(fd_);
        fd_ = -1;
        throw std::runtime_error("WriteAheadLog: mmap() failed");
    }

    mapped_size_ = size;
}

void WriteAheadLog::unmap_and_close() {
    if (mapped_ptr_) {
        msync(mapped_ptr_, mapped_size_, MS_SYNC);
        munmap(mapped_ptr_, mapped_size_);
        mapped_ptr_ = nullptr;
    }
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

void WriteAheadLog::sync_range(std::size_t offset, std::size_t length, bool blocking) {
    if (!mapped_ptr_) return;
    // msync requires page-aligned offset, so align down
    const std::size_t ps = page_size();
    const std::size_t aligned_offset = offset & ~(ps - 1);
    const std::size_t aligned_length = length + (offset - aligned_offset);

    msync(mapped_ptr_ + aligned_offset, aligned_length,
          blocking ? MS_SYNC : MS_ASYNC);
}

void WriteAheadLog::grow(std::size_t new_size) {
    new_size = align_to_page(new_size);
    if (mapped_ptr_) {
        munmap(mapped_ptr_, mapped_size_);
        mapped_ptr_ = nullptr;
    }
    if (ftruncate(fd_, static_cast<off_t>(new_size)) != 0) {
        throw std::runtime_error("WriteAheadLog: ftruncate() failed during grow");
    }
    mapped_ptr_ = static_cast<uint8_t*>(
        mmap(nullptr, new_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0)
    );
    if (mapped_ptr_ == MAP_FAILED) {
        mapped_ptr_ = nullptr;
        throw std::runtime_error("WriteAheadLog: mmap() failed during grow");
    }
    mapped_size_ = new_size;
}

#endif  // _WIN32

// ── Append ────────────────────────────────────────────────────────────────

uint64_t WriteAheadLog::append(std::span<const uint8_t> data) {
    // Frame: [magic(4B)][size(4B)][data(NB)][crc32(4B)]
    const std::size_t frame_size = 4 + 4 + data.size() + 4;

    // Check if we need to grow the file
    if (write_offset_ + frame_size > mapped_size_) {
        const std::size_t new_size = std::max(mapped_size_ * 2,
                                              write_offset_ + frame_size + page_size());
        grow(new_size);
    }

    uint8_t* ptr = mapped_ptr_ + write_offset_;

    // Write magic
    const uint32_t magic = WAL_MAGIC;
    std::memcpy(ptr, &magic, 4);

    // Write data size
    const uint32_t data_size = static_cast<uint32_t>(data.size());
    std::memcpy(ptr + 4, &data_size, 4);

    // Write data payload
    std::memcpy(ptr + 8, data.data(), data.size());

    // Write CRC32 checksum (computed over data bytes only)
    const uint32_t crc = compute_crc32(data.data(), data.size());
    std::memcpy(ptr + 8 + data.size(), &crc, 4);

    // Sync to disk BEFORE advancing the offset.
    // This guarantees that if the process crashes after append() returns,
    // the frame is durably on disk.
    sync_range(write_offset_, frame_size, /*blocking=*/true);

    write_offset_ += frame_size;
    return ++sequence_;
}

// ── Flush Async ───────────────────────────────────────────────────────────

void WriteAheadLog::flush_async() {
    if (write_offset_ > 0) {
        sync_range(0, write_offset_, /*blocking=*/false);
    }
}

// ── Replay ────────────────────────────────────────────────────────────────

uint64_t WriteAheadLog::replay(
    std::function<void(uint64_t seq, std::span<const uint8_t> data)> callback)
{
    if (!mapped_ptr_) return 0;

    std::size_t offset = 0;
    uint64_t replayed = 0;

    while (offset + 12 <= mapped_size_) {  // Minimum frame: 4+4+0+4 = 12 bytes
        // Read magic
        uint32_t magic;
        std::memcpy(&magic, mapped_ptr_ + offset, 4);
        if (magic != WAL_MAGIC) break;  // No more valid frames

        // Read data size
        uint32_t data_size;
        std::memcpy(&data_size, mapped_ptr_ + offset + 4, 4);

        const std::size_t frame_size = 4 + 4 + data_size + 4;

        // Bounds check
        if (offset + frame_size > mapped_size_) break;

        // Read and verify CRC32
        uint32_t stored_crc;
        std::memcpy(&stored_crc, mapped_ptr_ + offset + 8 + data_size, 4);

        const uint32_t computed_crc = compute_crc32(
            mapped_ptr_ + offset + 8, data_size
        );

        if (stored_crc != computed_crc) break;  // Corrupt frame (torn write)

        // Valid frame — invoke callback
        ++replayed;
        callback(replayed,
                 std::span<const uint8_t>(mapped_ptr_ + offset + 8, data_size));

        offset += frame_size;
    }

    // Set write offset and sequence to resume appending after the last valid frame
    write_offset_ = offset;
    sequence_ = replayed;

    return replayed;
}

}  // namespace zerolgr
