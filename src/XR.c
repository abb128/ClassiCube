#include "Core.h"

#ifdef CC_BUILD_OPENXR

#include <stdio.h>
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
#include "Window.h"

#ifndef CC_BUILD_X11
#error "Only X11 is supported for now"
#endif

#define XR_USE_GRAPHICS_API_OPENGL
#define XR_USE_PLATFORM_XLIB

#include <GL/glx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#define STEAMVR_LINUX

#ifdef STEAMVR_LINUX
// https://github.com/ValveSoftware/SteamVR-for-Linux/issues/421
#define OGL_TEST(prefix) { const GLubyte* ver = glGetString(GL_VERSION); if(ver == NULL){ /*XR_LOG(prefix " lost context?");*/ GLContext_Update(); } }
#else
#define OGL_TEST(prefix)
#endif

#define XR_LOG(fmt, ...) printf("[CC_XR] " fmt " \n", ##__VA_ARGS__)

#define TOKENPASTE(x, y) x ## y
#define TOKENPASTE2(x, y) TOKENPASTE(x, y)

#define CHK_XR(fname, expr)   XrResult TOKENPASTE2(result, __LINE__) = expr; \
                                if (TOKENPASTE2(result, __LINE__) != XR_SUCCESS) { \
                                    XR_LOG("ERROR!!! Call to " #fname " on line %d failed with code %d.\n", __LINE__, TOKENPASTE2(result, __LINE__)); \
                                    return false; \
                                } \
                                OGL_TEST(#fname)

#define CHK_XRQ(fname, expr)   XrResult TOKENPASTE2(result, __LINE__) = expr; \
                                if (TOKENPASTE2(result, __LINE__) != XR_SUCCESS) { \
                                    XR_LOG("ERROR!!! Call to " #fname " on line %d failed with code %d.\n", __LINE__, TOKENPASTE2(result, __LINE__)); \
                                    return; \ 
                                } \
                                OGL_TEST(#fname)


struct Swapchain {
    uint32_t width;
    uint32_t height;
    XrSwapchain handle;

    uint32_t image_count;
    uint32_t *images;
    GfxResourceID *framebuffers;
};
typedef struct Swapchain Swapchain;


static cc_bool init_fail = false;
cc_bool sessionActive = false;
cc_bool foreground = false;

static XrInstance instance = { 0 };
static XrSystemId systemId = XR_NULL_SYSTEM_ID;
static XrSession session = { 0 };
static XrSpace space = { 0 };

static XrViewConfigurationView *configViews = 0;
static Swapchain *swapchains = 0;

static uint32_t configViewCount = 0;
static uint32_t swapchainCount = 0;


static PFN_xrGetOpenGLGraphicsRequirementsKHR xrGetOpenGLGraphicsRequirementsKHR = 0;

XrVersion ConvertVersion(uint16_t major, uint16_t minor, uint32_t patch) {
    return ((uint64_t)patch) | (((uint64_t)minor) << 32) | (((uint64_t)major) << 48);
}

const char ApplicationName[] = GAME_APP_NAME;
const char EngineName[]      = GAME_APP_NAME;

void XR_ObtainInstance( void ) {
    const char OpenGLExtension[] = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
    const char* const enabledExtensionNames[] = {OpenGLExtension};

    const char ValidationLayer[] = "XR_APILAYER_LUNARG_core_validation";
    const char* const enabledApiLayerNames[] = {ValidationLayer};

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
        .enabledApiLayerCount = sizeof(enabledApiLayerNames) / sizeof(enabledApiLayerNames[0]),
        .enabledApiLayerNames = enabledApiLayerNames,
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
        XrInstanceProperties properties = { XR_TYPE_INSTANCE_PROPERTIES };
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
        Mem_Set(configViews, 0, viewCount * sizeof(XrViewConfigurationView));
        for(int i=0; i<viewCount; i++){
            configViews[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
        }

        CHK_XR(xrEnumerateViewConfigurationViews,
            xrEnumerateViewConfigurationViews(instance, systemId,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                viewCount,
                &viewCount,
                configViews
            )
        );

        configViewCount = viewCount;
    }

    // check requiremtnts
    // TODO: min is opengl 4.3 for steamvr
    {
        XrGraphicsRequirementsOpenGLKHR requirements = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };

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

    
    // create session
    {
        void* binding = GLContext_GetXRGraphicsBinding();

        XrSessionCreateInfo createInfo = {XR_TYPE_SESSION_CREATE_INFO};
        createInfo.next = binding;
        createInfo.systemId = systemId;

        CHK_XR(xrCreateSession, xrCreateSession(instance, &createInfo, &session));

        Mem_Free(binding);
    }

    {
        XrReferenceSpaceCreateInfo createInfo = {
            .type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
            .next = NULL,
            .referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE,
            .poseInReferenceSpace = {
                .orientation = { 0.0f, 0.0f, 0.0f, 1.0f },
                .position = { 0.0f, 0.0f, 0.0f }
            }
        };

        CHK_XR(xrCreateReferenceSpace, xrCreateReferenceSpace(session, &createInfo, &space));
    }
    
    
    XR_LOG(":)");
    
    

    // create swapchains
    // begin session

    // main loop

    // end session
    // destroy session

    // no access to hardware after this point
    return true;
}


int64_t XR_SelectSwapchainFormat(int count, int64_t* formats){
    const int64_t supported_formats[] = {
        GL_RGB10_A2,
        GL_RGBA16F,
        // The two below should only be used as a fallback, as they are linear color formats without enough bits for color
        // depth, thus leading to banding.
        //GL_RGBA16,
        //GL_RGB8,
        GL_RGBA8,
        GL_RGBA8_SNORM
    };
    for(int i=0; i<(sizeof(supported_formats)/sizeof(supported_formats[0])); i++){
        for(int j=0; j<count; j++){
            if(formats[j] == supported_formats[i]) return formats[j];
        }
    }

    XR_LOG("Failed to find any compatible format!!! Formats list:");

    for(int i=0; i<count; i++){
        printf("* %d\n", formats[i]);
    }

    Logger_Abort("Failed to find any compatible format.");
    return 0;
}

cc_bool XR_CreateSwapchains(void){
    uint32_t format_count = 0;
    CHK_XR(xrEnumerateSwapchainFormats,
        xrEnumerateSwapchainFormats(session, 0, &format_count, NULL)
    );

    int64_t *formats = (int64_t *)Mem_TryAlloc(format_count, sizeof(int64_t));
    CHK_XR(xrEnumerateSwapchainFormats,
        xrEnumerateSwapchainFormats(session, format_count, &format_count, formats)
    );

    int64_t format = XR_SelectSwapchainFormat(format_count, formats);

    Mem_Free(formats);

    swapchainCount = configViewCount;
    swapchains = Mem_TryAlloc(swapchainCount, sizeof(Swapchain));
    Mem_Set(swapchains, 0, swapchainCount * sizeof(Swapchain));

    for(int i=0; i<swapchainCount; i++){
        const XrViewConfigurationView *vp = &configViews[i];

        XrSwapchainCreateInfo createInfo = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
        createInfo.arraySize = 1;
        createInfo.format = format;
        createInfo.width = vp->recommendedImageRectWidth;
        createInfo.height = vp->recommendedImageRectHeight;
        createInfo.mipCount = 1;
        createInfo.faceCount = 1;
        createInfo.sampleCount = 1;
        createInfo.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;

        swapchains[i].width = createInfo.width;
        swapchains[i].height = createInfo.width;

        CHK_XR(xrCreateSwapchain,
            xrCreateSwapchain(session, &createInfo, &swapchains[i].handle)
        );;

        // get images
        int img_count;
        CHK_XR(xrEnumerateSwapchainImages,
            xrEnumerateSwapchainImages(swapchains[i].handle, 0, &img_count, NULL)
        );
        swapchains[i].image_count = img_count;

        swapchains[i].images = Mem_TryAlloc(sizeof(uint32_t), swapchains[i].image_count);
        swapchains[i].framebuffers = Mem_TryAlloc(sizeof(GfxResourceID), swapchains[i].image_count);

        XrSwapchainImageOpenGLKHR* images = Mem_TryAlloc(sizeof(XrSwapchainImageOpenGLKHR), swapchains[i].image_count);
        Mem_Set(images, 0, sizeof(XrSwapchainImageOpenGLKHR) * img_count);
        for(int j=0; j<img_count; j++){
            images[j].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
        }

        CHK_XR(xrEnumerateSwapchainImages,
            xrEnumerateSwapchainImages(swapchains[i].handle, img_count, &swapchains[i].image_count, images)
        );

        if(img_count != swapchains[i].image_count){
            Logger_Abort("Mismatch image count???");
        }

        for(int j=0; j<img_count; j++){
            if(images[j].type != XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR){
                //Logger_Abort("Expected OpenGL image base header");
                XR_LOG("WTF?? Swapchain image type is %d\n", images[j].type);
                return false;
            }

            XrSwapchainImageOpenGLKHR *glimage = (XrSwapchainImageOpenGLKHR *)&images[j];

            uint32_t image = glimage->image;

            swapchains[i].images[j] = image;

            swapchains[i].framebuffers[j] = Gfx_GenFramebuffer(createInfo.width, createInfo.height, image);
            XR_LOG("Generated framebuffer for image %d, id %d", image, swapchains[i].framebuffers[j]);
        }

        //XR_LOG("Created swapchain, size %d x %d. %d images", createInfo.width, createInfo.height, swapchains[i].image_count);
    }
}

cc_bool XR_BeginSession(void){
    XrSessionBeginInfo beginInfo = { XR_TYPE_SESSION_BEGIN_INFO };
    beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    CHK_XR(xrBeginSession, xrBeginSession(session, &beginInfo));

    XR_LOG("Beginning session!!!");
    XR_CreateSwapchains();

    sessionActive = true;
}

cc_bool XR_SessionStateChangedEvent(const XrEventDataSessionStateChanged *event) {
    switch(event->state){
        case XR_SESSION_STATE_IDLE:
            sessionActive = false;
            foreground = false;
            return true;
        case XR_SESSION_STATE_READY:
            return XR_BeginSession();
    }

    XR_LOG("Unhandled session state change to %d", event->state);

    return false;
}

static void XR_Tick(struct ScheduledTask* task){
    if(init_fail) return;

    XrEventDataBuffer event = { XR_TYPE_EVENT_DATA_BUFFER };

    XrResult result = xrPollEvent(instance, &event);
    if (result == XR_SUCCESS) {
        switch (event.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                const XrEventDataSessionStateChanged* session_state_changed_event =
                    (XrEventDataSessionStateChanged*)(&event);
                XR_SessionStateChangedEvent(session_state_changed_event);
                break;
            }
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
                const XrEventDataInstanceLossPending* instance_loss_pending_event =
                    (XrEventDataInstanceLossPending*)(&event);
                // ...
                break;
            }
        }
    }

    return;
}

cc_bool XR_IsActive(void){
    return (!init_fail) && (sessionActive) && (swapchainCount > 0);
}




struct XRFrameContext {
    XrFrameState frameState;

    XrViewState viewState;
    XrView views[2];

    int viewHead;
    int viewMax;
    int imageIndices[2];
};

struct XRFrameContext *XR_InitFrameContext() {
    struct XRFrameContext *result = Mem_TryAlloc(1, sizeof(struct XRFrameContext));
    Mem_Set(result, 0, sizeof(struct XRFrameContext));

    result->frameState.type = XR_TYPE_FRAME_STATE;
    result->viewState.type = XR_TYPE_VIEW_STATE;
    result->views[0].type = XR_TYPE_VIEW;
    result->views[1].type = XR_TYPE_VIEW;

    result->viewHead = 0;
    result->viewMax = 0;

    result->imageIndices[0] = -1;
    result->imageIndices[1] = -1;

    return result;
}
void XR_FreeFrameContext(struct XRFrameContext *ctx) {
    Mem_Free(ctx);
}

void XR_WaitFrame(struct XRFrameContext *ctx) {
    XrFrameWaitInfo frameWaitInfo = { XR_TYPE_FRAME_WAIT_INFO };

    CHK_XRQ(xrWaitFrame,
        xrWaitFrame(session, &frameWaitInfo, &ctx->frameState)
    );
}


struct Matrix ConvertOpenXRPoseToMatrix(XrPosef pose) {
    struct Matrix translation = Matrix_IdentityValue;
    Matrix_Translate(&translation, pose.position.x, pose.position.y, pose.position.z);

    float q0 = pose.orientation.x;
    float q1 = pose.orientation.y;
    float q2 = pose.orientation.z;
    float q3 = pose.orientation.w;

    struct Matrix rotation = {
        2.0f * (q0 * q0 + q1 * q1) - 1.0f ,
        2.0f * (q1 * q2 - q0 * q3)     ,
        2.0f * (q1 * q3 + q0 * q2)     ,
        0.0f,
        
        // Second row of the rotation matrix
        2.0f * (q1 * q2 + q0 * q3)     ,
        2.0f * (q0 * q0 + q2 * q2) - 1.0f ,
        2.0f * (q2 * q3 - q0 * q1)     ,
        0.0f,
        
        // Third row of the rotation matrix
        2.0f * (q1 * q3 - q0 * q2)     ,
        2.0f * (q2 * q3 + q0 * q1)     ,
        2.0f * (q0 * q0 + q3 * q3) - 1.0f ,
        0.0f,
    };


    struct Matrix result = Matrix_IdentityValue;
    Matrix_Mul(&result, &translation, &rotation);

    return result;
}


void XR_BeginFrame(struct XRFrameContext *ctx) {
    XrFrameBeginInfo frameBeginInfo = { XR_TYPE_FRAME_BEGIN_INFO };

    CHK_XRQ(xrBeginFrame,
        xrBeginFrame(session, &frameBeginInfo)
    );



    XrViewLocateInfo viewLocateInfo = { XR_TYPE_VIEW_LOCATE_INFO };
    viewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    viewLocateInfo.displayTime = ctx->frameState.predictedDisplayTime;
    viewLocateInfo.space = space;

    CHK_XRQ(xrLocateViews,
        xrLocateViews(session, &viewLocateInfo, &ctx->viewState, 2, &ctx->viewMax, ctx->views)
    );
}

/* Iterate this in a loop until it returns false */
cc_bool XR_RenderNextView(struct XRFrameContext *ctx, struct XRViewRender *view) {
    if(ctx->viewHead > 0){
        CHK_XR(xrReleaseSwapchainImage,
            xrReleaseSwapchainImage(swapchains[ctx->viewHead - 1].handle, NULL)
        );
    }

    if(ctx->viewHead >= ctx->viewMax) return false;

    XrView *current = &ctx->views[ctx->viewHead];
    XrPosef pose = current->pose;
    XrFovf fov = current->fov;

    view->pose = ConvertOpenXRPoseToMatrix(pose);
    
    float zNear = 0.05f;
    float zFar = (float)Game_ViewDistance;
    Matrix_Perspective(&view->projection, fov.angleLeft, fov.angleRight, fov.angleUp, fov.angleDown, zNear, zFar);


    
    uint32_t image_index;
    CHK_XR(xrAcquireSwapchainImage,
        xrAcquireSwapchainImage(swapchains[ctx->viewHead].handle, NULL, &image_index)
    );

    ctx->imageIndices[ctx->viewHead] = (int)image_index;

    view->framebuffer = swapchains[ctx->viewHead].framebuffers[image_index];


    XrSwapchainImageWaitInfo waitInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    waitInfo.timeout = XR_INFINITE_DURATION;
    CHK_XR(xrWaitSwapchainImage,
        xrWaitSwapchainImage(swapchains[ctx->viewHead].handle, &waitInfo)
    );


    ctx->viewHead++;


    return true;
}

void XR_SubmitFrame(struct XRFrameContext *ctx) {
    XrCompositionLayerProjectionView projViews[2] = { {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW} };
    for(int i=0; i<2; i++){
        projViews[i].pose = ctx->views[i].pose;
        projViews[i].fov = ctx->views[i].fov;
        projViews[i].subImage.swapchain = swapchains[i].handle;
        projViews[i].subImage.imageRect.extent.width = swapchains[i].width;
        projViews[i].subImage.imageRect.extent.height = swapchains[i].height;
        projViews[i].subImage.imageArrayIndex = 0;//ctx->imageIndices[i];
    }

    XrCompositionLayerProjection layerProj = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    layerProj.space = space;
    layerProj.viewCount = 2;
    layerProj.views = projViews;


    const XrCompositionLayerBaseHeader * const layers[] = {
        (XrCompositionLayerBaseHeader*)&layerProj
    };

    XrFrameEndInfo endInfo = { XR_TYPE_FRAME_END_INFO };
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.displayTime = ctx->frameState.predictedDisplayTime;
    endInfo.layerCount = 1;
    endInfo.layers = layers;

    CHK_XRQ(xrEndFrame,
        xrEndFrame(session, &endInfo)
    );
}


static void OnInit(void){
    XR_LOG("Hi");
    if(XR_Init()) {
	    ScheduledTask_Add(GAME_NET_TICKS, XR_Tick);
    }else{
        init_fail = true;
    }
}

static void OnFree(void){
    // TODO
}

struct IGameComponent XR_Component = {
    OnInit,
    OnFree
};

#endif // CC_BUILD_OPENXR