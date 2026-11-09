#include "crash_log.hpp"
#include "runtime.hpp"
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif

namespace {
static_assert(std::atomic<int>::is_always_lock_free, "Fatal-signal breadcrumbs require lock-free integer loads");
int descriptor = -1;
volatile std::sig_atomic_t handling = 0;
void integer(char*& cursor, int number) {
    if (number < 0) { *cursor++ = '-'; number = -number; }
    char reverse[16]; int count = 0;
    do { reverse[count++] = static_cast<char>('0' + number % 10); number /= 10; } while (number);
    while (count) *cursor++ = reverse[--count];
}
void literal(char*& cursor, const char* text) { while (*text) *cursor++ = *text++; }
void fatal_signal(int signal) {
    if (handling) std::_Exit(128 + signal);
    handling = 1;
    char buffer[256]; char* cursor = buffer;
    literal(cursor, "Fatal signal="); integer(cursor, signal);
    const auto& breadcrumb = wolf::crash_breadcrumb;
    literal(cursor, " command="); integer(cursor, breadcrumb.code.load(std::memory_order_relaxed));
    literal(cursor, " common="); integer(cursor, breadcrumb.common.load(std::memory_order_relaxed));
    literal(cursor, " map="); integer(cursor, breadcrumb.map.load(std::memory_order_relaxed));
    literal(cursor, " event="); integer(cursor, breadcrumb.event.load(std::memory_order_relaxed));
    literal(cursor, " original_offset="); integer(cursor, breadcrumb.offset.load(std::memory_order_relaxed));
    *cursor++ = '\n';
    if (descriptor >= 0) {
#ifdef _WIN32
        _write(descriptor, buffer, static_cast<unsigned>(cursor - buffer));
#else
        const auto ignored = write(descriptor, buffer, cursor - buffer); (void)ignored;
#endif
    }
    std::signal(signal, SIG_DFL); std::raise(signal); std::_Exit(128 + signal);
}
void install(void (*handler)(int)) {
    std::signal(SIGSEGV, handler); std::signal(SIGABRT, handler);
    std::signal(SIGILL, handler); std::signal(SIGFPE, handler);
#ifdef SIGBUS
    std::signal(SIGBUS, handler);
#endif
}
}
CrashLog::CrashLog(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
#ifdef _WIN32
    descriptor = _wopen(path.c_str(), _O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    descriptor = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
#endif
    if (descriptor >= 0) install(fatal_signal);
}
CrashLog::~CrashLog() {
    install(SIG_DFL);
    if (descriptor >= 0) {
#ifdef _WIN32
        _close(descriptor);
#else
        close(descriptor);
#endif
    }
    descriptor = -1;
}
