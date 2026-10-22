#include <list>
#include <vector>
#include <string.h>
#include <pthread.h>
#include <thread>
#include <cstring>
#include <jni.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <dlfcn.h>
#include "Includes/Logger.h"
#include "Includes/FileLogger.h"
#include "Includes/obfuscate.h"
#include "Includes/Utils.h"
#include "KittyMemory/MemoryPatch.h"
#include "Menu/Setup.h"

// Target library for GTA San Andreas FPS Unity IL2CPP
#define targetLibName OBFUSCATE("libil2cpp.so")

#include "Includes/Macros.h"
#include "Includes/FPS_HUD_Manager.h"

// We will run our hacks in a new thread so our while loop doesn't block process main thread
void *hack_thread(void *) {
    // 1. Install crash handler to catch native signals and log to /sdcard/Documents
    InstallNativeCrashHandler();

    LOG_FILE_I("=================================================");
    LOG_FILE_I("GTA SA FPS Mod Menu background thread started.");
    LOG_FILE_I("Log destination: %s", GetLogFilePath());
    LOG_FILE_I("Waiting for %s to be loaded...", (const char*)targetLibName);
    LOG_FILE_I("=================================================");

    // 2. Check if target lib is loaded (with fallback checks)
    int waitSeconds = 0;
    while (!isLibraryLoaded(targetLibName)) {
        // Fallback: check if libFileA.so or GTA native lib loaded
        if (isLibraryLoaded("libFileA.so")) {
            LOG_FILE_I("Detected fallback library: libFileA.so");
            break;
        }
        sleep(1);
        waitSeconds++;
        if (waitSeconds % 10 == 0) {
            LOG_FILE_D("Still waiting for game library (%s) to load... (%d seconds elapsed)", (const char*)targetLibName, waitSeconds);
        }
    }

    LOG_FILE_I("%s has been confirmed loaded!", (const char *) targetLibName);

#if defined(__aarch64__)
    // 3. Initialize HUD Manager and install hooks to track player and bot counts
    bool hooked = FPS_HUD_Manager::GetInstance()->InitializeHooks(targetLibName);
    if (hooked) {
        LOG_FILE_I("FPS HUD Manager initialized successfully with live BattleManager hooks!");
    } else {
        LOG_FILE_E("Failed to install inline hooks on target library!");
    }
#else 
    LOG_FILE_W("Architecture is not arm64-v8a. Skipping 64-bit hooks.");
#endif

    return NULL;
}

// JNI bindings for HUD Status Panel
jstring Native_GetHUDStatusText(JNIEnv *env, jobject thiz) {
    std::string text = FPS_HUD_Manager::GetInstance()->GetFormattedStatusText();
    return env->NewStringUTF(text.c_str());
}

jintArray Native_GetHUDStatusData(JNIEnv *env, jobject thiz) {
    HUDStatusInfo info = FPS_HUD_Manager::GetInstance()->GetStatusInfo();
    jint data[8];
    data[0] = info.totalPlayers;
    data[1] = info.botCount;
    data[2] = info.humanPlayers;
    data[3] = info.swatCount;
    data[4] = info.terrorCount;
    data[5] = info.worldState;
    data[6] = info.inBattle ? 1 : 0;
    data[7] = info.enabled ? 1 : 0;

    jintArray result = env->NewIntArray(8);
    env->SetIntArrayRegion(result, 0, 8, data);
    return result;
}

void Native_SetHUDEnabled(JNIEnv *env, jobject thiz, jboolean enabled) {
    FPS_HUD_Manager::GetInstance()->SetEnabled(enabled);
}

void Native_WriteDebugLog(JNIEnv *env, jobject thiz, jstring msg) {
    if (msg != NULL) {
        const char* str = env->GetStringUTFChars(msg, 0);
        LOG_FILE_I("[JAVA UI] %s", str);
        env->ReleaseStringUTFChars(msg, str);
    }
}

// Feature list for LGL Mod Menu
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;

    const char *features[] = {
            OBFUSCATE("Category_HUD Status Panel"),
            OBFUSCATE("Toggle_True_HUD Status Panel"),              // case 0: Floating HUD overlay
            OBFUSCATE("Toggle_True_Show Bots Count"),               // case 1: Show bot count
            OBFUSCATE("Toggle_True_Show Real Players"),             // case 2: Show real players count
            OBFUSCATE("Toggle_Show Team Breakdown"),                // case 3: SWAT vs Terror
            OBFUSCATE("Button_Export Status to Debug Log"),         // case 4: Dump snapshot to /sdcard/Documents
            OBFUSCATE("Category_Gameplay Features"),
            OBFUSCATE("Toggle_God Mode (Example)"),                 // case 5
            OBFUSCATE("SeekBar_Move Speed Multiplier_1_5"),         // case 6
    };

    int Total_Feature = (sizeof features / sizeof features[0]);
    ret = (jobjectArray)
            env->NewObjectArray(Total_Feature, env->FindClass(OBFUSCATE("java/lang/String")),
                                env->NewStringUTF(""));

    for (int i = 0; i < Total_Feature; i++)
        env->SetObjectArrayElement(ret, i, env->NewStringUTF(features[i]));

    return (ret);
}

void Changes(JNIEnv *env, jclass clazz, jobject obj,
             jint featNum, jstring featName, jint value,
             jboolean boolean, jstring str) {

    LOGD(OBFUSCATE("Feature name: %d - %s | Value: = %d | Bool: = %d | Text: = %s"), featNum,
         env->GetStringUTFChars(featName, 0), value,
         boolean, str != NULL ? env->GetStringUTFChars(str, 0) : "");

    switch (featNum) {
        case 0:
            FPS_HUD_Manager::GetInstance()->SetEnabled(boolean);
            LOG_FILE_I("Menu Setting: HUD Status Panel = %s", boolean ? "ENABLED" : "DISABLED");
            break;
        case 1:
            FPS_HUD_Manager::GetInstance()->SetShowBots(boolean);
            LOG_FILE_I("Menu Setting: Show Bots = %s", boolean ? "ENABLED" : "DISABLED");
            break;
        case 2:
            FPS_HUD_Manager::GetInstance()->SetShowReal(boolean);
            LOG_FILE_I("Menu Setting: Show Real Players = %s", boolean ? "ENABLED" : "DISABLED");
            break;
        case 3:
            FPS_HUD_Manager::GetInstance()->SetShowTeams(boolean);
            LOG_FILE_I("Menu Setting: Show Team Breakdown = %s", boolean ? "ENABLED" : "DISABLED");
            break;
        case 4: {
            HUDStatusInfo info = FPS_HUD_Manager::GetInstance()->GetStatusInfo();
            LOG_FILE_I("================ HUD STATUS EXPORT ================");
            LOG_FILE_I("InBattle    : %s", info.inBattle ? "YES" : "NO");
            LOG_FILE_I("WorldState  : %d (%s)", info.worldState,
                       info.worldState == 3 ? "Play" : (info.worldState == 1 ? "Load" : (info.worldState == 2 ? "Finish" : "None")));
            LOG_FILE_I("TotalPlayers: %d", info.totalPlayers);
            LOG_FILE_I("BotCount    : %d", info.botCount);
            LOG_FILE_I("HumanPlayers: %d", info.humanPlayers);
            LOG_FILE_I("SWAT Count  : %d | Terror Count: %d", info.swatCount, info.terrorCount);
            LOG_FILE_I("LibBase     : 0x%lx", (unsigned long)FPS_HUD_Manager::GetInstance()->GetLibBase());
            LOG_FILE_I("Hooks Active: %s", FPS_HUD_Manager::GetInstance()->AreHooksInstalled() ? "YES" : "NO");
            LOG_FILE_I("Log File    : %s", GetLogFilePath());
            LOG_FILE_I("===================================================");
            break;
        }
        case 5:
            LOG_FILE_I("Menu Setting: God Mode = %s", boolean ? "ON" : "OFF");
            break;
        case 6:
            LOG_FILE_I("Menu Setting: Speed Multiplier = %d", value);
            break;
    }
}

__attribute__((constructor))
void lib_main() {
    // Create background worker thread
    pthread_t ptid;
    pthread_create(&ptid, NULL, hack_thread, NULL);
}

int RegisterMenu(JNIEnv *env) {
    JNINativeMethod methods[] = {
            {OBFUSCATE("Icon"), OBFUSCATE("()Ljava/lang/String;"), reinterpret_cast<void *>(Icon)},
            {OBFUSCATE("IconWebViewData"),  OBFUSCATE("()Ljava/lang/String;"), reinterpret_cast<void *>(IconWebViewData)},
            {OBFUSCATE("IsGameLibLoaded"),  OBFUSCATE("()Z"), reinterpret_cast<void *>(isGameLibLoaded)},
            {OBFUSCATE("Init"),  OBFUSCATE("(Landroid/content/Context;Landroid/widget/TextView;Landroid/widget/TextView;)V"), reinterpret_cast<void *>(Init)},
            {OBFUSCATE("SettingsList"),  OBFUSCATE("()[Ljava/lang/String;"), reinterpret_cast<void *>(SettingsList)},
            {OBFUSCATE("GetFeatureList"),  OBFUSCATE("()[Ljava/lang/String;"), reinterpret_cast<void *>(GetFeatureList)},
            {OBFUSCATE("GetHUDStatusText"), OBFUSCATE("()Ljava/lang/String;"), reinterpret_cast<void *>(Native_GetHUDStatusText)},
            {OBFUSCATE("GetHUDStatusData"), OBFUSCATE("()[I"), reinterpret_cast<void *>(Native_GetHUDStatusData)},
            {OBFUSCATE("SetHUDEnabled"),    OBFUSCATE("(Z)V"), reinterpret_cast<void *>(Native_SetHUDEnabled)},
            {OBFUSCATE("WriteDebugLog"),    OBFUSCATE("(Ljava/lang/String;)V"), reinterpret_cast<void *>(Native_WriteDebugLog)},
    };

    jclass clazz = env->FindClass(OBFUSCATE("com/android/support/Menu"));
    if (!clazz)
        return JNI_ERR;
    if (env->RegisterNatives(clazz, methods, sizeof(methods) / sizeof(methods[0])) != 0)
        return JNI_ERR;
    return JNI_OK;
}

int RegisterPreferences(JNIEnv *env) {
    JNINativeMethod methods[] = {
            {OBFUSCATE("Changes"), OBFUSCATE("(Landroid/content/Context;ILjava/lang/String;IZLjava/lang/String;)V"), reinterpret_cast<void *>(Changes)},
    };
    jclass clazz = env->FindClass(OBFUSCATE("com/android/support/Preferences"));
    if (!clazz)
        return JNI_ERR;
    if (env->RegisterNatives(clazz, methods, sizeof(methods) / sizeof(methods[0])) != 0)
        return JNI_ERR;
    return JNI_OK;
}

int RegisterMain(JNIEnv *env) {
    JNINativeMethod methods[] = {
            {OBFUSCATE("CheckOverlayPermission"), OBFUSCATE("(Landroid/content/Context;)V"), reinterpret_cast<void *>(CheckOverlayPermission)},
    };
    jclass clazz = env->FindClass(OBFUSCATE("com/android/support/Main"));
    if (!clazz)
        return JNI_ERR;
    if (env->RegisterNatives(clazz, methods, sizeof(methods) / sizeof(methods[0])) != 0)
        return JNI_ERR;

    return JNI_OK;
}

extern "C"
JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env;
    vm->GetEnv((void **) &env, JNI_VERSION_1_6);
    if (RegisterMenu(env) != 0)
        return JNI_ERR;
    if (RegisterPreferences(env) != 0)
        return JNI_ERR;
    if (RegisterMain(env) != 0)
        return JNI_ERR;
    return JNI_VERSION_1_6;
}
