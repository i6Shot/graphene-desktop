/*
 * This file is part of graphene-desktop, the desktop environment of VeltOS.
 * Copyright (C) 2016 Velt Technologies, Aidan Shafran <zelbrium@gmail.com>
 * Licensed under the Apache License 2 <www.apache.org/licenses/LICENSE-2.0>.
 *
 * The GrapheneWindow struct is for communication between the Window Manager
 * (wm.c) and its delegates (such as the panel and task switcher). The WM
 * creates a GrapheneWindow for any windows a delegate should care about,
 * and passes them to the delegates. The WM uses Mutter, so it could just pass
 * a MetaWindow ref, but that makes delegates very Mutter-dependant and gives
 * them more info than necessary.
 */

#ifndef __GRAPHENE_WINDOW_H__
#define __GRAPHENE_WINDOW_H__

typedef struct _GrapheneWindow GrapheneWindow;
typedef void (*GrapheneWindowNotify)(GrapheneWindow *window);

typedef enum
{
	GRAPHENE_WINDOW_FLAG_NORMAL = 0,
	GRAPHENE_WINDOW_FLAG_MINIMIZED = 1,
	GRAPHENE_WINDOW_FLAG_ATTENTION = 2,
	GRAPHENE_WINDOW_FLAG_FOCUSED = 4,
	GRAPHENE_WINDOW_FLAG_SKIP_TASKBAR = 8
} GrapheneWindowFlags;

struct _GrapheneWindow
{
	// Delegates ignore
	void *wm;
	void *window;

	// Delegates may use but not modify
	const char *title;
	char *icon;
	GrapheneWindowFlags flags;
	
	void (*show)(GrapheneWindow *window);
	void (*minimize)(GrapheneWindow *window);
	void (*setIconBox)(GrapheneWindow *window, double x, double y, double width, double height); // Screen coords
};

#endif
