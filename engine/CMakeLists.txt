cmake_minimum_required(VERSION 3.20)
project(ZeroLGR VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Find Dependencies ──────────────────────────────────────────────────────
# ZeroMQ: Use vcpkg or system-installed cppzmq
find_package(cppzmq QUIET)

if(NOT cppzmq_FOUND)
    # Try pkg-config as fallback
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(ZMQ IMPORTED_TARGET libzmq)
    endif()

    if(NOT ZMQ_FOUND)
        message(WARNING
            "cppzmq not found. Building without ZMQ support.\n"
            "Install via: vcpkg install cppzmq  OR  apt install libzmq3-dev libcppzmq-dev")
        set(HAS_ZMQ FALSE)
    else()
        set(HAS_ZMQ TRUE)
    endif()
else()
    set(HAS_ZMQ TRUE)
endif()

# ── Engine Core Library (no ZMQ dependency) ────────────────────────────────
# This allows testing the ring buffer, WAL, and ledger independently.

add_library(zerolgr_core STATIC
    src/wal.cpp
    src/ledger.cpp
)

target_include_directories(zerolgr_core PUBLIC include)

# Platform-specific linking
if(WIN32)
    # Windows: no additional libs needed for mmap (uses Win32 API)
else()
    target_link_libraries(zerolgr_core PUBLIC pthread)
endif()

# ── Engine Executable (requires ZMQ) ───────────────────────────────────────

if(HAS_ZMQ)
    add_executable(zerolgr_engine
        src/main.cpp
        src/server.cpp
    )

    target_include_directories(zerolgr_engine PRIVATE include)
    target_link_libraries(zerolgr_engine PRIVATE zerolgr_core)

    if(cppzmq_FOUND)
        target_link_libraries(zerolgr_engine PRIVATE cppzmq)
    else()
        target_link_libraries(zerolgr_engine PRIVATE PkgConfig::ZMQ)
        # cppzmq is header-only, so we just need the zmq library
        # and the cppzmq header. If installed via apt: libcppzmq-dev
    endif()
else()
    message(STATUS "Skipping zerolgr_engine build (no ZMQ)")
endif()

# ── Tests ──────────────────────────────────────────────────────────────────

option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
    enable_testing()

    # Test: Ring Buffer
    add_executable(test_ring_buffer tests/test_ring_buffer.cpp)
    target_link_libraries(test_ring_buffer PRIVATE zerolgr_core)
    add_test(NAME RingBufferTest COMMAND test_ring_buffer)

    # Test: WAL
    add_executable(test_wal tests/test_wal.cpp)
    target_link_libraries(test_wal PRIVATE zerolgr_core)
    add_test(NAME WALTest COMMAND test_wal)

    # Test: Ledger
    add_executable(test_ledger tests/test_ledger.cpp)
    target_link_libraries(test_ledger PRIVATE zerolgr_core)
    add_test(NAME LedgerTest COMMAND test_ledger)
endif()
