#ifndef CC_XR_H
#define CC_XR_H

#include "Core.h"
#include "Vectors.h"
#include "Graphics.h"

#ifdef CC_BUILD_OPENXR

struct IGameComponent;
extern struct IGameComponent XR_Component;


struct XRFrameContext;

struct XRViewRender {
    struct Matrix pose;       // view matrix
    struct Matrix projection; // projection matrix
    GfxResourceID framebuffer;
};


/* Whether or not the XR rendering methods should be called. */
cc_bool XR_IsActive(void);


struct XRFrameContext *XR_InitFrameContext();
void XR_FreeFrameContext(struct XRFrameContext *ctx);

void XR_WaitFrame(struct XRFrameContext *ctx);

void XR_BeginFrame(struct XRFrameContext *ctx);

/* Iterate this in a loop until it returns false */
cc_bool XR_RenderNextView(struct XRFrameContext *ctx, struct XRViewRender *view);

void XR_SubmitFrame(struct XRFrameContext *ctx);


#endif // CC_BUILD_OPENXR
#endif // CC_XR_H