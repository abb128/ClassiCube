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
#include "Entity.h"
#include "Input.h"
#include "Gui.h"

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
#include "xrmath.h"

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


#define STR_COPY(const_str, dest, max_size) Mem_Copy(dest, const_str, min(sizeof(const_str), max_size));


const char GAME_ACTION_SET_NAME[] = "ingameactions";
const char GAME_ACTION_SET_NAME_LOCAL[] = "In-Game Actions";

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
static XrSpace headspace = { 0 };
static XrActionSet actionSetGame;

static XrViewConfigurationView *configViews = 0;
static Swapchain *swapchains = 0;

static uint32_t configViewCount = 0;
static uint32_t swapchainCount = 0;



const char GAME_ACTION_WALK_NAME[] = "walk";
const char GAME_ACTION_WALK_NAME_LOCAL[] = "Walk";
const char GAME_ACTION_ROTATE_NAME[] = "rotate";
const char GAME_ACTION_ROTATE_NAME_LOCAL[] = "Rotate";
const char GAME_ACTION_JUMP_NAME[] = "jump";
const char GAME_ACTION_JUMP_NAME_LOCAL[] = "Jump";
static struct {
    XrAction walk_vec2;   // usually left joystick
    XrAction rotate_vec2; // usually right joystick

    XrAction jump_bool;   // usually right A
} GameActions;

cc_bool XR_InitGameActions( void ) {
    XrActionCreateInfo actionInfo = { XR_TYPE_ACTION_CREATE_INFO };

    actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    STR_COPY(GAME_ACTION_JUMP_NAME,       actionInfo.actionName,          XR_MAX_ACTION_NAME_SIZE);
    STR_COPY(GAME_ACTION_JUMP_NAME_LOCAL, actionInfo.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE);
    CHK_XR(xrCreateAction, xrCreateAction(actionSetGame, &actionInfo, &GameActions.jump_bool));

    actionInfo.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
    STR_COPY(GAME_ACTION_WALK_NAME,       actionInfo.actionName,          XR_MAX_ACTION_NAME_SIZE);
    STR_COPY(GAME_ACTION_WALK_NAME_LOCAL, actionInfo.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE);
    CHK_XR(xrCreateAction, xrCreateAction(actionSetGame, &actionInfo, &GameActions.walk_vec2));

    actionInfo.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
    STR_COPY(GAME_ACTION_ROTATE_NAME,       actionInfo.actionName,          XR_MAX_ACTION_NAME_SIZE);
    STR_COPY(GAME_ACTION_ROTATE_NAME_LOCAL, actionInfo.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE);
    CHK_XR(xrCreateAction, xrCreateAction(actionSetGame, &actionInfo, &GameActions.rotate_vec2));


    // suggested bindings for oculus

    XrPath oculusProfile;
    CHK_XR(xrStringToPath, xrStringToPath(instance, "/interaction_profiles/oculus/touch_controller", &oculusProfile));

    XrPath AButton, leftJoystick, rightJoystick;
    CHK_XR(xrStringToPath, xrStringToPath(instance, "/user/hand/right/input/a/click", &AButton));
    CHK_XR(xrStringToPath, xrStringToPath(instance, "/user/hand/right/input/thumbstick", &rightJoystick));
    CHK_XR(xrStringToPath, xrStringToPath(instance, "/user/hand/left/input/thumbstick", &leftJoystick));

    XrActionSuggestedBinding bindings[3] = {
        {.action = GameActions.jump_bool, .binding = AButton},
        {.action = GameActions.walk_vec2, .binding = leftJoystick},
        {.action = GameActions.rotate_vec2, .binding = rightJoystick},
    };

    XrInteractionProfileSuggestedBinding suggestedBindings = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
    suggestedBindings.interactionProfile = oculusProfile;
    suggestedBindings.suggestedBindings = bindings;
    suggestedBindings.countSuggestedBindings = sizeof(bindings) / sizeof(bindings[0]);

    CHK_XR(xrSuggestInteractionProfileBindings, xrSuggestInteractionProfileBindings(instance, &suggestedBindings));


    // attach

    XrSessionActionSetsAttachInfo attachInfo = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    attachInfo.countActionSets = 1;
    attachInfo.actionSets = &actionSetGame;
    CHK_XR(xrAttachSessionActionSets, xrAttachSessionActionSets(session, &attachInfo));
}

static PFN_xrGetOpenGLGraphicsRequirementsKHR xrGetOpenGLGraphicsRequirementsKHR = 0;

XrVersion ConvertVersion(uint16_t major, uint16_t minor, uint32_t patch) {
    return ((uint64_t)patch) | (((uint64_t)minor) << 32) | (((uint64_t)major) << 48);
}

const char APPLICATION_NAME[] = GAME_APP_NAME;
const char ENGINE_NAME[]      = GAME_APP_NAME;

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


    STR_COPY(APPLICATION_NAME, createInfo.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE);
    STR_COPY(ENGINE_NAME,      createInfo.applicationInfo.engineName,      XR_MAX_ENGINE_NAME_SIZE);

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

    // create space
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

        createInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;

        CHK_XR(xrCreateReferenceSpace, xrCreateReferenceSpace(session, &createInfo, &headspace));
    }
    

    // create game action set
    {
        XrActionSetCreateInfo createInfo = { XR_TYPE_ACTION_SET_CREATE_INFO };
        STR_COPY(GAME_ACTION_SET_NAME, createInfo.actionSetName, XR_MAX_ACTION_SET_NAME_SIZE);
        STR_COPY(GAME_ACTION_SET_NAME_LOCAL, createInfo.localizedActionSetName, XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE);
        createInfo.priority = 1;

        CHK_XR(xrCreateActionSet, xrCreateActionSet(instance, &createInfo, &actionSetGame));

        if(!XR_InitGameActions()) return false;
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
        createInfo.width = vp->recommendedImageRectWidth * 2;
        createInfo.height = vp->recommendedImageRectHeight * 2;
        createInfo.mipCount = 1;
        createInfo.faceCount = 1;
        createInfo.sampleCount = 1;
        createInfo.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;

        swapchains[i].width = createInfo.width;
        swapchains[i].height = createInfo.height;

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

    float cam_y;
    XrPosef cam_pose;
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

    return result;
}
void XR_FreeFrameContext(struct XRFrameContext *ctx) {
    Mem_Free(ctx);
}

static float yawOffset = 0.0f;
static struct Matrix yawOffsetMatrix = Matrix_IdentityValue;
static struct Matrix yawOffsetInverseMatrix = Matrix_IdentityValue;
void XR_WaitFrame(struct XRFrameContext *ctx) {
    XrFrameWaitInfo frameWaitInfo = { XR_TYPE_FRAME_WAIT_INFO };

    CHK_XRQ(xrWaitFrame,
        xrWaitFrame(session, &frameWaitInfo, &ctx->frameState)
    );
}


void CopyXrMatrix4x4fToMatrix(struct Matrix *result, const XrMatrix4x4f *src) {
    result->row1.X = src->m[0];
    result->row1.Y = src->m[1];
    result->row1.Z = src->m[2];
    result->row1.W = src->m[3];
    result->row2.X = src->m[4+0];
    result->row2.Y = src->m[4+1];
    result->row2.Z = src->m[4+2];
    result->row2.W = src->m[4+3];
    result->row3.X = src->m[4+4+0];
    result->row3.Y = src->m[4+4+1];
    result->row3.Z = src->m[4+4+2];
    result->row3.W = src->m[4+4+3];
    result->row4.X = src->m[4+4+4+0];
    result->row4.Y = src->m[4+4+4+1];
    result->row4.Z = src->m[4+4+4+2];
    result->row4.W = src->m[4+4+4+3];
}

struct Matrix ConvertOpenXRPoseToMatrix(XrPosef pose, cc_bool position_invert) {
    XrMatrix4x4f result_m;
    XrMatrix4x4f_CreateViewMatrix(&result_m, &pose.position, &pose.orientation);

    struct Matrix result = Matrix_IdentityValue;
    CopyXrMatrix4x4fToMatrix(&result, &result_m);

    return result;
}


void XR_BeginFrame(struct XRFrameContext *ctx) {
    XrFrameBeginInfo frameBeginInfo = { XR_TYPE_FRAME_BEGIN_INFO };

    CHK_XRQ(xrBeginFrame,
        xrBeginFrame(session, &frameBeginInfo)
    );

    // view position
    XrViewLocateInfo viewLocateInfo = { XR_TYPE_VIEW_LOCATE_INFO };
    viewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    viewLocateInfo.displayTime = ctx->frameState.predictedDisplayTime;
    viewLocateInfo.space = space;

    CHK_XRQ(xrLocateViews,
        xrLocateViews(session, &viewLocateInfo, &ctx->viewState, 2, &ctx->viewMax, ctx->views)
    );

    // head position
    XrSpaceLocation headPose = { XR_TYPE_SPACE_LOCATION, NULL, 0, {{0, 0, 0, 1}, {0, 0, 0}} };
	CHK_XR(xrLocateSpace, xrLocateSpace(headspace, space, ctx->frameState.predictedDisplayTime, &headPose));
    ctx->cam_pose = headPose.pose;
}

cc_bool XR_RenderNextView(struct XRFrameContext *ctx, struct XRViewRender *view) {
    if(ctx->viewHead > 0){
        CHK_XR(xrReleaseSwapchainImage,
            xrReleaseSwapchainImage(swapchains[ctx->viewHead - 1].handle, NULL)
        );
    }

    if(ctx->viewHead >= ctx->viewMax) return false;

    XrView *current = &ctx->views[ctx->viewHead];
    XrPosef pose = current->pose;

    // We subtract head XZ, because we handle XZ movement separately.
    pose.position.x -= ctx->cam_pose.position.x;
    pose.position.z -= ctx->cam_pose.position.z;

    XrFovf fov = current->fov;

    struct Matrix in = ConvertOpenXRPoseToMatrix(pose, false);
    Matrix_Mul(&view->pose, &yawOffsetMatrix, &in);

    XrMatrix4x4f projection_matrix;
    XrMatrix4x4f_CreateProjectionFov(&projection_matrix, GRAPHICS_OPENGL, current->fov, 0.05f, (float)Game_ViewDistance);
    
    CopyXrMatrix4x4fToMatrix(&view->projection, &projection_matrix);
    
    uint32_t image_index;
    CHK_XR(xrAcquireSwapchainImage,
        xrAcquireSwapchainImage(swapchains[ctx->viewHead].handle, NULL, &image_index)
    );

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
        projViews[i].subImage.imageArrayIndex = 0;
        projViews[i].subImage.imageRect.offset.x = 0;
        projViews[i].subImage.imageRect.offset.y = 0;
        projViews[i].subImage.imageRect.extent.width = swapchains[i].width;
        projViews[i].subImage.imageRect.extent.height = swapchains[i].height;
    }

    XrCompositionLayerProjection layerProj = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    layerProj.space = space;
	layerProj.layerFlags = XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT;
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

static cc_bool wasMovementInitialized = false;
static float xMove = 0.0f;
static float yMove = 0.0f;
void XR_GetMovement(float *x, float *y) {
    *x += xMove;
    *y += yMove;
}

const struct LocalPlayerInput xrInput = {
    .GetMovement = &XR_GetMovement,
};

cc_bool XR_GameInputTick(struct XRFrameContext* ctx, double delta) {
    if(!wasMovementInitialized){
        XR_LOG("Initializing movement");
        for(struct LocalPlayerInput *input = &LocalPlayer_Instance.input; input; input = input->next){
            if(input->next == NULL) {
                input->next = &xrInput;
                wasMovementInitialized = true;
                break;
            }
        }
    }
    XrActiveActionSet activeActionSet = { actionSetGame, XR_NULL_PATH };
    XrActionsSyncInfo syncInfo = { XR_TYPE_ACTIONS_SYNC_INFO };
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeActionSet;
    CHK_XR(xrSyncActions, xrSyncActions(session, &syncInfo));



    XrActionStateGetInfo getInfo = { XR_TYPE_ACTION_STATE_GET_INFO };

    // query jump
    XrActionStateBoolean jumpState = { XR_TYPE_ACTION_STATE_BOOLEAN };
    getInfo.action = GameActions.jump_bool;
    CHK_XR(xrGetActionStateBoolean, xrGetActionStateBoolean(session, &getInfo, &jumpState));

    // query walk
    XrActionStateVector2f walkState = { XR_TYPE_ACTION_STATE_VECTOR2F };
    getInfo.action = GameActions.walk_vec2;
    CHK_XR(xrGetActionStateVector2f, xrGetActionStateVector2f(session, &getInfo, &walkState));;

    // query rotate
    XrActionStateVector2f rotateState = { XR_TYPE_ACTION_STATE_VECTOR2F };
    getInfo.action = GameActions.rotate_vec2;
    CHK_XR(xrGetActionStateVector2f, xrGetActionStateVector2f(session, &getInfo, &rotateState));


    // set jump and fly    
    XrPressed[KEYBIND_JUMP] = jumpState.currentState && jumpState.isActive;
    XrPressed[KEYBIND_FLY_UP] = rotateState.isActive && (rotateState.currentState.y > 0.5f);
    XrPressed[KEYBIND_FLY_DOWN] = rotateState.isActive && (rotateState.currentState.y < -0.5f);

    // set move
    xMove = walkState.isActive ? walkState.currentState.x : 0.0f;
    yMove = walkState.isActive ? -walkState.currentState.y : 0.0f;

    // rotate
    if(rotateState.isActive){
        static cc_bool didReachLimit = false;
        if(Math_AbsF(rotateState.currentState.x) > 0.666){
            if(!didReachLimit){
                yawOffset += (rotateState.currentState.x > 0 ? 1.0f : -1.0f) * (3.14159265358f/4.0f);
                Matrix_RotateY(&yawOffsetMatrix, yawOffset);
                Matrix_RotateY(&yawOffsetInverseMatrix, -yawOffset);
            }
            didReachLimit = true;
        }else if(Math_AbsF(rotateState.currentState.x) < 0.4) {
            didReachLimit = false;
        }

        // smooth rotation
        //yawOffset += rotateState.currentState.x * delta * 8.0;
    }

    // set player position
    {
        float currPoseX = ctx->cam_pose.position.x;
        float currPoseZ = ctx->cam_pose.position.z;

        static float lastX = 0.0f;
        static float lastZ = 0.0f;


        Vec3 difference = Vec3_Create3(currPoseX-lastX, 0.0f, currPoseZ-lastZ);

        // no idea why we need inverse here
        Vec3_Transform(&difference, &difference, &yawOffsetInverseMatrix);

        Vec3_AddBy(&LocalPlayer_Instance.Interp.Next.Pos, &difference);


        lastX = currPoseX;
        lastZ = currPoseZ;
    }

    // set player yaw and pitch
    {
        XrPosef poseRotationOnly = ctx->cam_pose;
        poseRotationOnly.position.x = 0.0f;
        poseRotationOnly.position.y = 0.0f;
        poseRotationOnly.position.z = 0.0f;

        struct Matrix headMatrixIn = ConvertOpenXRPoseToMatrix(poseRotationOnly, false);
        struct Matrix headMatrix = Matrix_IdentityValue;
        Matrix_Mul(&headMatrix, &yawOffsetMatrix, &headMatrixIn);

        float yaw = Math_Atan2(headMatrix.row3.Z, -headMatrix.row1.Z) * MATH_RAD2DEG;
        float pitch = Math_Atan2(headMatrix.row2.Y, headMatrix.row2.Z) * MATH_RAD2DEG;

        if(pitch < -90.0f) {
            yaw += 180.0f;
            pitch = -89.9f;
        }else if(pitch > 90.0f) {
            yaw -= 180.0f;
            pitch = 90.0f;
        }

        LocalPlayer_Instance.Base.Yaw        = yaw;
        LocalPlayer_Instance.Interp.Next.Yaw = yaw;

        LocalPlayer_Instance.Base.Pitch        = pitch;
        LocalPlayer_Instance.Interp.Next.Pitch = pitch;
    }

    Gui.InputGrab = false;
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