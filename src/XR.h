#ifndef CC_XR_H
#define CC_XR_H

#include "Graphics.h"
#include "String.h"
#include "Platform.h"
#include "Funcs.h"
#include "Game.h"
#include "ExtMath.h"
#include "Event.h"
#include "Block.h"
#include "Options.h"
#include "Bitmap.h"
#include "Chat.h"

struct IGameComponent;
extern struct IGameComponent XR_Component;

cc_bool XR_Init(void);
cc_bool XR_IsActive(void);


#endif