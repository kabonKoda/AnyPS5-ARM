#include "SceTypes.hpp"
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <pthread.h>
#include <string>
#include <thread>

extern "C" {
int APS5_VABI scePthreadCreate(Pthread* thread, const PthreadAttr* attr, PthreadEntry entry, void* arg, const char* name);
int APS5_VABI scePthreadJoin(Pthread thread, void** retval);
Pthread APS5_VABI scePthreadSelf();
int APS5_VABI scePthreadRename(Pthread thread, const char* name);
int APS5_VABI scePthreadGetname(Pthread thread, char* name);
void APS5_VABI scePthreadTestcancel();
}

#ifdef __APPLE__
static constexpr std::size_t HOST_NAME_LIMIT = 63;
#else
static constexpr std::size_t HOST_NAME_LIMIT = 15;
#endif
static constexpr std::size_t GUEST_NAME_LIMIT = 31;

static void Require(bool value) { if (!value) std::abort(); }

static std::string HostName() {
    char buffer[128] = {};
    Require(pthread_getname_np(pthread_self(), buffer, sizeof(buffer)) == 0);
    return buffer;
}

static std::string GuestName(Pthread thread) {
    char buffer[64] = {};
    Require(scePthreadGetname(thread, buffer) == 0);
    return buffer;
}

static void RequireNames(Pthread thread, const std::string& name) {
    Require(GuestName(thread) == name.substr(0, GUEST_NAME_LIMIT));
    Require(HostName() == name.substr(0, HOST_NAME_LIMIT));
}

struct WorkerContext {
    Pthread thread = nullptr;
    std::atomic<int> stage{0};
};

static void WaitForStage(const WorkerContext& context, int stage) {
    while (context.stage.load(std::memory_order_acquire) < stage) std::this_thread::yield();
}

static void* APS5_VABI Worker(void* arg) {
    auto& context = *static_cast<WorkerContext*>(arg);
    RequireNames(scePthreadSelf(), "worker-start");
    context.stage.store(1, std::memory_order_release);
    WaitForStage(context, 2);
#ifdef __APPLE__
    Require(HostName() == "worker-start");
#endif
    scePthreadSelf();
    Require(HostName() == "worker-renamed");
    RequireNames(scePthreadSelf(), "worker-renamed");
    context.stage.store(3, std::memory_order_release);
    WaitForStage(context, 4);
    const std::string longName(70, 'w');
#ifdef __APPLE__
    Require(HostName() == "worker-renamed");
#endif
    scePthreadTestcancel();
    Require(HostName() == longName.substr(0, HOST_NAME_LIMIT));
    RequireNames(scePthreadSelf(), longName);
    return nullptr;
}

int main() {
    const Pthread self = scePthreadSelf();
    Require(self != nullptr);
    Require(scePthreadRename(self, "unity-main") == 0);
    RequireNames(self, "unity-main");
    const std::string medium(20, 'm');
    Require(scePthreadRename(self, medium.c_str()) == 0);
    RequireNames(self, medium);
    const std::string longName(70, 'l');
    Require(scePthreadRename(self, longName.c_str()) == 0);
    RequireNames(self, longName);

    WorkerContext context;
    Require(scePthreadCreate(&context.thread, nullptr, Worker, &context, "worker-start") == 0);
    WaitForStage(context, 1);
    Require(scePthreadRename(context.thread, "worker-renamed") == 0);
    Require(GuestName(context.thread) == "worker-renamed");
    context.stage.store(2, std::memory_order_release);
    WaitForStage(context, 3);
    const std::string workerLong(70, 'w');
    Require(scePthreadRename(context.thread, workerLong.c_str()) == 0);
    Require(GuestName(context.thread) == workerLong.substr(0, GUEST_NAME_LIMIT));
    context.stage.store(4, std::memory_order_release);
    Require(scePthreadJoin(context.thread, nullptr) == 0);
    RequireNames(self, longName);
}
