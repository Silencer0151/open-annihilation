// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The stand-ins for the functions Windows XP lacks. On every system: a slim
// lock keeps four threads' increments whole, readers share it and a writer
// excludes them, a condition wait hands work between threads and times out,
// and a one-time initialisation runs once for many threads and again after
// failing. With MinGW, on Windows: the file stand-ins agree with the
// system's own calls for a file this test writes, the functions under the
// system's names lock, wait and run once, and the steady clock reads the
// performance counter.
#include "oa/platform/xp_runtime.hpp"

#include "oa/base/threads.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>

#if defined(__MINGW32__)
// The declarations of Windows 7: the test calls the later functions, which
// an XP build's executables define themselves.
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#include <windows.h>

#include <chrono>
#include <cwchar>
#include <string>
#endif

namespace {

namespace threads = oa::base::threads;
namespace xp = oa::platform::xp_runtime;

int failures = 0;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

constexpr int worker_count = 4;
constexpr int increments_per_worker = 5000;

struct Counting {
    uintptr_t lock{};
    int counter{};
};

void count(void* argument) {
    auto& counting = *static_cast<Counting*>(argument);
    for (int step = 0; step < increments_per_worker; ++step) {
        xp::lock_exclusive(counting.lock);
        ++counting.counter;
        xp::unlock_exclusive(counting.lock);
    }
}

void test_exclusive_lock() {
    Counting counting;
    std::array<threads::Thread, worker_count> workers{};
    for (auto& worker : workers)
        CHECK(threads::start_thread(worker, count, &counting));
    for (auto& worker : workers)
        threads::join_thread(worker);
    CHECK(counting.counter == worker_count * increments_per_worker);
    CHECK(counting.lock == 0);
}

void test_shared_lock() {
    uintptr_t lock = 0;
    xp::lock_shared(lock);
    xp::lock_shared(lock);
    CHECK(!xp::try_lock_exclusive(lock));
    xp::unlock_shared(lock);
    CHECK(!xp::try_lock_exclusive(lock));
    xp::unlock_shared(lock);
    CHECK(xp::try_lock_exclusive(lock));
    CHECK(!xp::try_lock_exclusive(lock));
    xp::unlock_exclusive(lock);
    CHECK(lock == 0);
}

struct Handover {
    uintptr_t lock{};
    uintptr_t condition{};
    int value{};
    bool taken{};
};

void take(void* argument) {
    auto& handover = *static_cast<Handover*>(argument);
    xp::lock_exclusive(handover.lock);
    while (handover.value == 0)
        xp::wait_condition(handover.condition, handover.lock, xp::wait_forever, false);
    handover.taken = true;
    xp::unlock_exclusive(handover.lock);
    xp::wake_condition(handover.condition);
}

void test_condition() {
    Handover handover;
    threads::Thread taker;
    CHECK(threads::start_thread(taker, take, &handover));
    threads::sleep_ms(5);
    xp::lock_exclusive(handover.lock);
    handover.value = 1;
    xp::unlock_exclusive(handover.lock);
    xp::wake_condition(handover.condition);
    xp::lock_shared(handover.lock);
    while (!handover.taken)
        xp::wait_condition(handover.condition, handover.lock, xp::wait_forever, true);
    xp::unlock_shared(handover.lock);
    threads::join_thread(taker);
    CHECK(handover.taken);

    // A wait nobody wakes runs out of time, and the lock is held again.
    xp::lock_exclusive(handover.lock);
    CHECK(!xp::wait_condition(handover.condition, handover.lock, 20, false));
    CHECK(!xp::try_lock_exclusive(handover.lock));
    xp::unlock_exclusive(handover.lock);
}

struct Once {
    uintptr_t word{};
    std::atomic<int> runs{};
    std::atomic<int> agreed{};
};

alignas(4) int once_result = 0;

bool initialise(void* argument, void** result) {
    auto& once = *static_cast<Once*>(argument);
    once.runs.fetch_add(1);
    threads::sleep_ms(10);
    *result = &once_result;
    return true;
}

void run_initialisation(void* argument) {
    auto& once = *static_cast<Once*>(argument);
    void* result = nullptr;
    if (xp::run_once(once.word, initialise, &once, &result) && result == &once_result)
        once.agreed.fetch_add(1);
}

bool fail(void* argument, void**) {
    static_cast<Once*>(argument)->runs.fetch_add(1);
    return false;
}

void test_run_once() {
    Once once;
    std::array<threads::Thread, worker_count> workers{};
    for (auto& worker : workers)
        CHECK(threads::start_thread(worker, run_initialisation, &once));
    for (auto& worker : workers)
        threads::join_thread(worker);
    CHECK(once.runs.load() == 1);
    CHECK(once.agreed.load() == worker_count);

    Once failing;
    CHECK(!xp::run_once(failing.word, fail, &failing, nullptr));
    CHECK(!xp::run_once(failing.word, fail, &failing, nullptr));
    CHECK(failing.runs.load() == 2);
    CHECK(failing.word == 0);
}

#if defined(__MINGW32__)
/// The by-handle information class of a file's 128-bit identifier, which
/// Windows 8 added and the stand-in does not read.
constexpr int file_id_information_class = 18;

/// Writes a small file in the temporary folder and opens it for reading.
///
/// @param[out] path the file's path
/// @return its handle, or INVALID_HANDLE_VALUE
HANDLE open_test_file(std::wstring& path) {
    wchar_t folder[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, folder) == 0)
        return INVALID_HANDLE_VALUE;
    path = std::wstring(folder) + L"oa-xp-runtime-test.txt";
    const HANDLE written = CreateFileW(
        path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (written == INVALID_HANDLE_VALUE)
        return written;
    DWORD count = 0;
    WriteFile(written, "seven!\n", 7, &count, nullptr);
    CloseHandle(written);
    return CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
}

void test_file_stand_ins() {
    std::wstring path;
    const HANDLE file = open_test_file(path);
    CHECK(file != INVALID_HANDLE_VALUE);
    if (file == INVALID_HANDLE_VALUE)
        return;

    FILE_STANDARD_INFO system_standard{};
    FILE_STANDARD_INFO native_standard{};
    CHECK(GetFileInformationByHandleEx(
        file, FileStandardInfo, &system_standard, sizeof(system_standard)
    ));
    CHECK(
        xp::file_information_from_native(
            file, FileStandardInfo, &native_standard, sizeof(native_standard)
        )
    );
    CHECK(native_standard.EndOfFile.QuadPart == 7);
    CHECK(native_standard.EndOfFile.QuadPart == system_standard.EndOfFile.QuadPart);
    CHECK(native_standard.Directory == system_standard.Directory);

    FILE_BASIC_INFO system_basic{};
    FILE_BASIC_INFO native_basic{};
    CHECK(GetFileInformationByHandleEx(file, FileBasicInfo, &system_basic, sizeof(system_basic)));
    CHECK(
        xp::file_information_from_native(file, FileBasicInfo, &native_basic, sizeof(native_basic))
    );
    CHECK(native_basic.LastWriteTime.QuadPart == system_basic.LastWriteTime.QuadPart);
    CHECK(native_basic.FileAttributes == system_basic.FileAttributes);
    CHECK(!xp::file_information_from_native(
        file, file_id_information_class, &native_basic, sizeof(native_basic)
    ));

    wchar_t system_path[MAX_PATH * 2]{};
    wchar_t native_path[MAX_PATH * 2]{};
    const DWORD system_length = GetFinalPathNameByHandleW(file, system_path, MAX_PATH * 2, 0);
    const uint32_t native_length = xp::final_path_from_device(file, native_path, MAX_PATH * 2, 0);
    CHECK(system_length > 0);
    CHECK(native_length == system_length);
    CHECK(_wcsicmp(native_path, system_path) == 0);
    std::printf("final path: system %ls, stand-in %ls\n", system_path, native_path);
    CHECK(xp::final_path_from_device(file, native_path, 4, 0) == native_length + 1);
    CloseHandle(file);
    DeleteFileW(path.c_str());
}

/// Runs a one-time initialisation through the system's name for the call.
BOOL CALLBACK count_once(PINIT_ONCE, PVOID parameter, PVOID* context) {
    ++*static_cast<int*>(parameter);
    *context = nullptr;
    return TRUE;
}

void test_windows_names() {
    SRWLOCK lock = SRWLOCK_INIT;
    CONDITION_VARIABLE condition = CONDITION_VARIABLE_INIT;
    AcquireSRWLockExclusive(&lock);
    CHECK(!TryAcquireSRWLockExclusive(&lock));
    CHECK(!SleepConditionVariableSRW(&condition, &lock, 10, 0));
    ReleaseSRWLockExclusive(&lock);
    AcquireSRWLockShared(&lock);
    ReleaseSRWLockShared(&lock);
    WakeAllConditionVariable(&condition);
    INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    int runs = 0;
    CHECK(InitOnceExecuteOnce(&once, count_once, &runs, nullptr));
    CHECK(InitOnceExecuteOnce(&once, count_once, &runs, nullptr));
    CHECK(runs == 1);
    const DWORD slot = FlsAlloc(nullptr);
    CHECK(slot != FLS_OUT_OF_INDEXES);
    CHECK(FlsSetValue(slot, &runs));
    CHECK(FlsGetValue(slot) == &runs);
    CHECK(GetThreadId(GetCurrentThread()) == GetCurrentThreadId());
    CHECK(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS) >= 1);
}

/// How many times test_steady_clock reads the clock.
constexpr int clock_readings = 100;

/// Converts a performance counter reading to nanoseconds, rounded down.
///
/// @param counter the reading
/// @param frequency the counter's ticks per second
/// @return the reading in nanoseconds
int64_t counter_nanoseconds(int64_t counter, int64_t frequency) {
    constexpr int64_t nanoseconds_per_second = 1'000'000'000;
    return counter / frequency * nanoseconds_per_second +
           counter % frequency * nanoseconds_per_second / frequency;
}

/// Checks that the steady clock reads the performance counter, not the time
/// of day: each reading lies between the counter's own readings just before
/// and just after it, allowing a nanosecond for a clock that rounds to the
/// nearest.
void test_steady_clock() {
    LARGE_INTEGER frequency{};
    CHECK(QueryPerformanceFrequency(&frequency));
    if (frequency.QuadPart <= 0)
        return;
    for (int reading = 0; reading < clock_readings; ++reading) {
        LARGE_INTEGER before{};
        LARGE_INTEGER after{};
        QueryPerformanceCounter(&before);
        const auto since_start = std::chrono::steady_clock::now().time_since_epoch();
        QueryPerformanceCounter(&after);
        const int64_t steady =
            std::chrono::duration_cast<std::chrono::nanoseconds>(since_start).count();
        const int64_t earliest = counter_nanoseconds(before.QuadPart, frequency.QuadPart);
        const int64_t latest = counter_nanoseconds(after.QuadPart, frequency.QuadPart) + 1;
        CHECK(steady >= earliest && steady <= latest);
        if (steady < earliest || steady > latest) {
            std::fprintf(
                stderr,
                "steady clock %lld ns, performance counter %lld to %lld ns\n",
                static_cast<long long>(steady),
                static_cast<long long>(earliest),
                static_cast<long long>(latest)
            );
            return;
        }
    }
}
#endif

} // namespace

int main() {
    test_exclusive_lock();
    test_shared_lock();
    test_condition();
    test_run_once();
#if defined(__MINGW32__)
    test_file_stand_ins();
    test_windows_names();
    test_steady_clock();
#endif
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("xp-runtime checks passed");
    return 0;
}
