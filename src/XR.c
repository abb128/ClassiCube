#define XR_USE_GRAPHICS_API_OPENGL

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

#define XR_LOG(fmt, ...) printf("[CC_XR] " fmt " \n", ##__VA_ARGS__)

#define TOKENPASTE(x, y) x ## y
#define TOKENPASTE2(x, y) TOKENPASTE(x, y)

#define CHK_XR(fname, expr)   XrResult TOKENPASTE2(result, __LINE__) = expr; \
                                if (TOKENPASTE2(result, __LINE__) != XR_SUCCESS) { \
                                    XR_LOG("Call to " #fname " on line %d failed with code %d.\n", __LINE__, TOKENPASTE2(result, __LINE__)); \
                                    return false; \
                                }

static cc_bool init_fail = false;

static XrInstance instance = { 0 };
static XrSystemId systemId = XR_NULL_SYSTEM_ID;
static XrSession session = { 0 };
static XrSpace space = { 0 };

static XrViewConfigurationView *configViews = 0;
static uint32_t configViewCount = 0;


static PFN_xrGetOpenGLGraphicsRequirementsKHR xrGetOpenGLGraphicsRequirementsKHR = 0;

XrVersion ConvertVersion(uint16_t major, uint16_t minor, uint32_t patch) {
    return ((uint64_t)patch) | (((uint64_t)minor) << 32) | (((uint64_t)major) << 48);
}

const char ApplicationName[] = GAME_APP_NAME;
const char EngineName[]      = GAME_APP_NAME;

void XR_ObtainInstance( void ) {
    const char OpenGLExtension[] = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;

    const char* const enabledExtensionNames[] = {OpenGLExtension};

    XrInstanceCreateInfo createInfo = {
        .type = XR_TYPE_INSTANCE_CREATE_INFO,
        .next = NULL,
        .createFlags = 0,
        .applicationInfo = {
            .applicationName = { 0 },
            .applicationVersion = 1,
            .engineName = { 0 },
            .engineVersion = 1,
            .apiVersion = XR_MAKE_VERSION(1, 0, 24)
        },
        .enabledApiLayerCount = 0,
        .enabledApiLayerNames = NULL,
        .enabledExtensionCount = sizeof(enabledExtensionNames) / sizeof(enabledExtensionNames[0]),
        .enabledExtensionNames = enabledExtensionNames
    };

    Mem_Copy(createInfo.applicationInfo.applicationName, ApplicationName, min(sizeof(ApplicationName), XR_MAX_APPLICATION_NAME_SIZE));
    Mem_Copy(createInfo.applicationInfo.engineName,      EngineName,      min(sizeof(EngineName), XR_MAX_ENGINE_NAME_SIZE));

    XrResult result = xrCreateInstance(&createInfo, &instance);

    if(result == XR_SUCCESS){
        XR_LOG("XrInstance obtained");
    } else {
        init_fail = true;
        XR_LOG("XrInstance initialization failed!!!");
    }
}

cc_bool XR_Init( void ) {
    XR_ObtainInstance();
    if(init_fail) return false;

    XR_LOG("Properties");
    {
        XrInstanceProperties properties = { 0 };
        CHK_XR(xrGetInstanceProperties, xrGetInstanceProperties(instance, &properties));
        XR_LOG("OpenXR runtime: %s", properties.runtimeName);
    }
    
    XR_LOG("System");
    {
        XrSystemGetInfo getInfo = {
            .type = XR_TYPE_SYSTEM_GET_INFO,
            .next = NULL,
            .formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY
        };

        CHK_XR(xrGetSystem, xrGetSystem(instance, &getInfo, &systemId));
    }

    XR_LOG("View");
    {
        int viewCount = 0;
        CHK_XR(xrEnumerateViewConfigurationViews,
            xrEnumerateViewConfigurationViews(instance, systemId,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                0,
                &viewCount,
                NULL
            )
        );

        configViews = Mem_TryAlloc(viewCount, sizeof(XrViewConfigurationView));
        CHK_XR(xrEnumerateViewConfigurationViews,
            xrEnumerateViewConfigurationViews(instance, systemId,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                viewCount,
                &viewCount,
                configViews
            )
        );
    }

    // check requiremtnts
    {
        XrGraphicsRequirementsOpenGLKHR requirements = { 0 };

        CHK_XR(xrGetInstanceProcAddr,
            xrGetInstanceProcAddr(instance, "xrGetOpenGLGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&xrGetOpenGLGraphicsRequirementsKHR)
        );

        CHK_XR(xrGetOpenGLGraphicsRequirementsKHR,
            xrGetOpenGLGraphicsRequirementsKHR(instance, systemId, &requirements)
        );

        XR_LOG("Minimum OpenGL version: %d.%d.%d",
            XR_VERSION_MAJOR(requirements.minApiVersionSupported),
            XR_VERSION_MINOR(requirements.minApiVersionSupported),
            XR_VERSION_PATCH(requirements.minApiVersionSupported)
        );

        XR_LOG("Maximum OpenGL version: %d.%d.%d",
            XR_VERSION_MAJOR(requirements.maxApiVersionSupported),
            XR_VERSION_MINOR(requirements.maxApiVersionSupported),
            XR_VERSION_PATCH(requirements.maxApiVersionSupported)
        );
    }

    /*
    // create session
    {

    }

    // begin session
    {
        XrSessionBeginInfo beginInfo = {XR_TYPE_SESSION_BEGIN_INFO};
        beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        CHK_XR(xrBeginSession, xrBeginSession(session, &beginInfo));
    }

    */
    /*
    {
        XrReferenceSpaceCreateInfo createInfo = {
            .type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
            .next = NULL,
            .referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE,
            .poseInReferenceSpace = { 0 }
        };

        CHK_XR(xrCreateReferenceSpace, xrCreateReferenceSpace())
    }
    */
    

    // create swapchains
    // begin session

    // main loop

    // end session
    // destroy session

    // no access to hardware after this point
    return true;
}


static void OnInit(void){
    XR_LOG("Hi");
    if(!XR_Init()) {
        init_fail = true;
    }
}

struct IGameComponent XR_Component = {
    OnInit
};