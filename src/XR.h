#ifndef CC_XR_H
#define CC_XR_H

#include "Core.h"
#include "Vectors.h"
#include "Graphics.h"

#ifdef CC_BUILD_OPENXR

struct IGameComponent;
extern struct IGameComponent XR_Component;

/* Opaque internal struct */
struct XRFrameContext;

/* Data to be used by the renderer to render a single view. */
struct XRViewRender {
    struct Matrix pose;
    struct Matrix projection;
    GfxResourceID framebuffer;
};


/* Whether or not XR is active. If false, don't call the frame functions */
cc_bool XR_IsActive(void);

/* Initializes a frame context, should be done at the beginning of a frame. */
/* Don't forget to free the frame context via XR_FreeFrameContext */
struct XRFrameContext *XR_InitFrameContext();

/* Frees a frame context, should be called at the end of a frame */
void XR_FreeFrameContext(struct XRFrameContext *ctx);

/* Waits for the next frame */
void XR_WaitFrame(struct XRFrameContext *ctx);

/* Readies and begins frame rendering. Should be called immediately before */
/* rendering is started. Next create an empty XR_RenderNextView and call */
/* XR_RenderNextView in a loop until it returns false. */
void XR_BeginFrame(struct XRFrameContext *ctx);

/* Returns false if no more views are available. This should be called in */
/* a loop after XR_BeginFrame, and the loop should be broken once false. */
/* If true, XRViewRender has been filled out and the renderer should render */
/* to the given framebuffer using the given pose and projection. */
cc_bool XR_RenderNextView(struct XRFrameContext *ctx, struct XRViewRender *view);

/* Submits frame, call after done rendering, before XR_FreeFrameContext */
void XR_SubmitFrame(struct XRFrameContext *ctx);


#endif // CC_BUILD_OPENXR
#endif // CC_XR_H