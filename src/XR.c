#include <stdio.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "XR.h"
#include "Graphics.h"
#include "String.h"
#include "Platform.h"
#include "Funcs.h"
#include "Game.h"
#include "Logger.h"
#include "ExtMath.h"
#include "Event.h"
#include "Block.h"
#include "Options.h"
#include "Bitmap.h"

#define XR_CHECK(fname, expr)   XrResult result ##__LINE__ = expr; \
                                if (result ##__LINE__ != XR_SUCCESS) { \
                                    printf("Call to " #fname " failed with code %d.\n", result ##__LINE__); \
                                    return; \
                                }

#define XR_LOG(fmt, ...) printf("[CC_XR] " fmt " \n", ##__VA_ARGS__)

static cc_bool has_instance = false;
static XrInstance instance = { 0 };

XrVersion ConvertVersion(uint16_t major, uint16_t minor, uint32_t patch) {
    return ((uint64_t)patch) | (((uint64_t)minor) << 32) | (((uint64_t)major) << 48);
}

const char ApplicationName[] = GAME_APP_NAME;
const char EngineName[]      = GAME_APP_NAME;

void XR_ObtainInstance( void ) {
    XrInstanceCreateInfo createInfo = {
        .type = XR_TYPE_INSTANCE_CREATE_INFO,
        .next = NULL,
        .createFlags = 0,
        .applicationInfo = {
            .applicationName = { 0 },
            .applicationVersion = 1,
            .engineName = { 0 },
            .engineVersion = 1,
            .apiVersion = ConvertVersion(1, 0, 24)
        },
        .enabledApiLayerCount = 0,
        .enabledApiLayerNames = NULL,
        .enabledExtensionCount = 0,
        .enabledExtensionNames = NULL
    };

    Mem_Copy(createInfo.applicationInfo.applicationName, ApplicationName, min(sizeof(ApplicationName), XR_MAX_APPLICATION_NAME_SIZE));
    Mem_Copy(createInfo.applicationInfo.engineName,      EngineName,      min(sizeof(EngineName), XR_MAX_ENGINE_NAME_SIZE));

    XrResult result = xrCreateInstance(&createInfo, &instance);

    if(result == XR_SUCCESS){
        has_instance = true;
        XR_LOG("XrInstance obtained");
    } else {
        XR_LOG("XrInstance initialization failed!!!");
    }
}

void XR_Init( void ) {
    XR_ObtainInstance();
    if(!has_instance) return;

    XrInstanceProperties properties = { 0 };
    XR_CHECK(xrGetInstanceProperties, xrGetInstanceProperties(instance, &properties));
    XR_LOG("OpenXR runtime: %s", properties.runtimeName);
}


static void OnInit(void){
    XR_Init();
}

struct IGameComponent XR_Component = {
    OnInit
};