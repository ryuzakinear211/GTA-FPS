#ifndef FILE_LOGGER_H
#define FILE_LOGGER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <ucontext.h>
#include <android/log.h>
#include <pthread.h>

#define FILE_LOG_TAG "GTA_FPS_HUD"

static pthread_mutex_t g_LogMutex = PTHREAD_MUTEX_INITIALIZER;
static char g_LogFilePath[256] = {0};
static bool g_LogInitialized = false;

// Function to find or create a writable log directory and file
static const char* GetLogFilePath() {
    if (g_LogInitialized && g_LogFilePath[0] != '\0') {
        return g_LogFilePath;
    }

    const char* candidatePaths[] = {
        "/sdcard/Documents",
        "/storage/emulated/0/Documents",
        "/sdcard",
        "/storage/emulated/0"
    };

    for (size_t i = 0; i < sizeof(candidatePaths) / sizeof(candidatePaths[0]); ++i) {
        struct stat st;
        if (stat(candidatePaths[i], &st) == 0 && S_ISDIR(st.st_mode)) {
            snprintf(g_LogFilePath, sizeof(g_LogFilePath), "%s/fps_hud_debug.log", candidatePaths[i]);
            FILE* testFp = fopen(g_LogFilePath, "a");
            if (testFp != NULL) {
                fclose(testFp);
                g_LogInitialized = true;
                return g_LogFilePath;
            }
        } else {
            // Try creating the directory if it does not exist
            mkdir(candidatePaths[i], 0777);
            snprintf(g_LogFilePath, sizeof(g_LogFilePath), "%s/fps_hud_debug.log", candidatePaths[i]);
            FILE* testFp = fopen(g_LogFilePath, "a");
            if (testFp != NULL) {
                fclose(testFp);
                g_LogInitialized = true;
                return g_LogFilePath;
            }
        }
    }

    // Default fallback
    snprintf(g_LogFilePath, sizeof(g_LogFilePath), "/sdcard/Documents/fps_hud_debug.log");
    g_LogInitialized = true;
    return g_LogFilePath;
}

static inline void WriteFileLog(const char* level, const char* fmt, ...) {
    pthread_mutex_lock(&g_LogMutex);

    const char* path = GetLogFilePath();
    FILE* fp = fopen(path, "a");

    char timeBuf[32];
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    if (tm_info) {
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tm_info);
    } else {
        snprintf(timeBuf, sizeof(timeBuf), "UNKNOWN_TIME");
    }

    char messageBuf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(messageBuf, sizeof(messageBuf), fmt, args);
    va_end(args);

    // Also log to Android Logcat
    __android_log_print(ANDROID_LOG_INFO, FILE_LOG_TAG, "[%s] %s", level, messageBuf);

    if (fp != NULL) {
        fprintf(fp, "[%s] [%s] %s\n", timeBuf, level, messageBuf);
        fflush(fp);
        fclose(fp);
    }

    pthread_mutex_unlock(&g_LogMutex);
}

#define LOG_FILE_I(...) WriteFileLog("INFO", __VA_ARGS__)
#define LOG_FILE_W(...) WriteFileLog("WARN", __VA_ARGS__)
#define LOG_FILE_E(...) WriteFileLog("ERROR", __VA_ARGS__)
#define LOG_FILE_D(...) WriteFileLog("DEBUG", __VA_ARGS__)

// Native Crash Handler
static struct sigaction old_sa_segv;
static struct sigaction old_sa_bus;
static struct sigaction old_sa_fpe;
static struct sigaction old_sa_ill;
static struct sigaction old_sa_abrt;
static uintptr_t g_TargetLibBase = 0;

static const char* GetSignalName(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV (Segmentation Fault)";
        case SIGBUS:  return "SIGBUS (Bus Error)";
        case SIGFPE:  return "SIGFPE (Floating Point Exception)";
        case SIGILL:  return "SIGILL (Illegal Instruction)";
        case SIGABRT: return "SIGABRT (Abort)";
        default:      return "UNKNOWN_SIGNAL";
    }
}

static void NativeCrashHandler(int sig, siginfo_t* info, void* context) {
    ucontext_t* uc = (ucontext_t*)context;
    uintptr_t faultAddr = (uintptr_t)info->si_addr;
    uintptr_t pc = 0, lr = 0, sp = 0;

#if defined(__aarch64__)
    if (uc) {
        pc = (uintptr_t)uc->uc_mcontext.pc;
        sp = (uintptr_t)uc->uc_mcontext.sp;
        lr = (uintptr_t)uc->uc_mcontext.regs[30];
    }
#elif defined(__arm__)
    if (uc) {
        pc = (uintptr_t)uc->uc_mcontext.arm_pc;
        sp = (uintptr_t)uc->uc_mcontext.arm_sp;
        lr = (uintptr_t)uc->uc_mcontext.arm_lr;
    }
#endif

    const char* path = GetLogFilePath();
    FILE* fp = fopen(path, "a");
    if (fp != NULL) {
        char timeBuf[32];
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        if (tm_info) strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tm_info);
        else snprintf(timeBuf, sizeof(timeBuf), "CRASH_TIME");

        fprintf(fp, "\n=======================================================\n");
        fprintf(fp, "[%s] [CRASH DETECTED] %s (Signal %d, Code: %d)\n", timeBuf, GetSignalName(sig), sig, info->si_code);
        fprintf(fp, "  Fault Address : 0x%lx\n", (unsigned long)faultAddr);
        fprintf(fp, "  ProgramCounter: 0x%lx\n", (unsigned long)pc);
        fprintf(fp, "  Link Register : 0x%lx\n", (unsigned long)lr);
        fprintf(fp, "  Stack Pointer : 0x%lx\n", (unsigned long)sp);
        if (g_TargetLibBase != 0) {
            fprintf(fp, "  LibBase       : 0x%lx\n", (unsigned long)g_TargetLibBase);
            if (pc >= g_TargetLibBase) {
                fprintf(fp, "  PC Offset     : 0x%lx\n", (unsigned long)(pc - g_TargetLibBase));
            }
        }
#if defined(__aarch64__)
        if (uc) {
            fprintf(fp, "  X0: 0x%016lx  X1: 0x%016lx\n", (unsigned long)uc->uc_mcontext.regs[0], (unsigned long)uc->uc_mcontext.regs[1]);
            fprintf(fp, "  X2: 0x%016lx  X3: 0x%016lx\n", (unsigned long)uc->uc_mcontext.regs[2], (unsigned long)uc->uc_mcontext.regs[3]);
            fprintf(fp, "  X4: 0x%016lx  X5: 0x%016lx\n", (unsigned long)uc->uc_mcontext.regs[4], (unsigned long)uc->uc_mcontext.regs[5]);
            fprintf(fp, "  X29(FP): 0x%016lx  X30(LR): 0x%016lx\n", (unsigned long)uc->uc_mcontext.regs[29], (unsigned long)uc->uc_mcontext.regs[30]);
        }
#endif
        fprintf(fp, "=======================================================\n\n");
        fflush(fp);
        fclose(fp);
    }

    __android_log_print(ANDROID_LOG_FATAL, FILE_LOG_TAG, "CRASH DETECTED: %s at fault addr 0x%lx, pc: 0x%lx", GetSignalName(sig), (unsigned long)faultAddr, (unsigned long)pc);

    // Call original handler or default
    if (sig == SIGSEGV && old_sa_segv.sa_sigaction) old_sa_segv.sa_sigaction(sig, info, context);
    else if (sig == SIGBUS && old_sa_bus.sa_sigaction) old_sa_bus.sa_sigaction(sig, info, context);
    else if (sig == SIGFPE && old_sa_fpe.sa_sigaction) old_sa_fpe.sa_sigaction(sig, info, context);
    else if (sig == SIGILL && old_sa_ill.sa_sigaction) old_sa_ill.sa_sigaction(sig, info, context);
    else if (sig == SIGABRT && old_sa_abrt.sa_sigaction) old_sa_abrt.sa_sigaction(sig, info, context);
    else signal(sig, SIG_DFL);
}

static void InstallNativeCrashHandler() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = NativeCrashHandler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;

    sigaction(SIGSEGV, &sa, &old_sa_segv);
    sigaction(SIGBUS,  &sa, &old_sa_bus);
    sigaction(SIGFPE,  &sa, &old_sa_fpe);
    sigaction(SIGILL,  &sa, &old_sa_ill);
    sigaction(SIGABRT, &sa, &old_sa_abrt);

    LOG_FILE_I("Native Crash Handler registered for SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT.");
}

#endif // FILE_LOGGER_H
