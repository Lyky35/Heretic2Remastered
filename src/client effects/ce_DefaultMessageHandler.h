//
// ce_DefaultMessageHandler.h
//
// Copyright 1998 Raven Software
//

#pragma once

#include "ce_Message.h"

struct client_entity_s; //mxd. Forward declaration: without it this prototype declares a different (prototype-scoped) type.

extern void CE_DefaultMsgHandler(struct client_entity_s* self, CE_Message_t* msg);