#if __STDC_VERSION__ >= 199901L
#define _XOPEN_SOURCE 600
#else
#define _XOPEN_SOURCE 500
#endif

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glxext.h>

#include "win.h"

#include "a.h"
#include "deckard.h"
#include "ui.h"


static Display* dpy = NULL;
static XVisualInfo* vis = NULL;
static GLXContext ctx = 0;


/****************************************************
 win_* API
****************************************************/

win_id win_open()
{
	XSetWindowAttributes attrs;
	Window root = RootWindow(dpy, vis->screen);
	attrs.colormap = XCreateColormap(
		dpy,
		root,
		vis->visual,
		AllocNone);
	attrs.background_pixmap = None;
	attrs.border_pixel = 0;
	attrs.event_mask =
		StructureNotifyMask
		| ButtonPressMask
		| KeyPressMask
		| ExposureMask
		| VisibilityChangeMask;

	Window win = XCreateWindow(
		dpy,
		root,
		0, 0,
		100, 100,
		0,
		vis->depth,
		InputOutput,
		vis->visual, CWBorderPixel | CWColormap | CWEventMask,
		&attrs);

	if (!win) {
		fprintf(stderr, "XCreateWindow failed\n");
		exit(EXIT_FAILURE);
	}

	XStoreName(dpy, win, DECKARD_WINDOW_TITLE);
	XMapWindow(dpy, win);

	return win;
}


void win_make_current(win_id id)
{
	glXMakeCurrent(dpy, id, ctx);
}

int win_poll_event(struct win_event* e)
{
	if (!XPending(dpy)) return 0;

	XEvent xe;
	XNextEvent(dpy, &xe);

	switch (xe.type) {
		case VisibilityNotify:
			break;
		case Expose:
			break;
		case KeyPress: {
			e->type = EV_KEYDOWN;
			e->key.sym = XkbKeycodeToKeysym(
				dpy,
				xe.xkey.keycode,
				0, 0);
			break; }
		case ButtonPress:
			e->type = EV_BUTTONDOWN;
			break;
		case ButtonRelease:
			e->type = EV_BUTTONUP;
			break;
	}

	return 1;
}

void win_begin(win_id id, int* width_return, int* height_return)
{
	win_make_current(id);

	Window root;
	int x,y;
	unsigned int width, height, border_width, depth;
	XGetGeometry(dpy, id, &root, &x, &y, &width, &height, &border_width, &depth);

	glViewport(0, 0, width, height); CHKGL;
	if (width_return != NULL) *width_return = width;
	if (height_return != NULL) *height_return = height;
}

void win_flip(win_id id)
{
	glXSwapBuffers(dpy, id);
}



/****************************************************
 main
****************************************************/

static int is_extension_supported(const char* extensions, const char* extension)
{
	const char* p0 = extensions;
	const char* p1 = p0;
	for (;;) {
		while (*p1 != ' ' && *p1 != '\0') p1++;
		if (memcmp(extension, p0, p1 - p0) == 0) return 1;
		if (*p1 == '\0') return 0;
		p0 = p1++;
	}
}

static int tmp_ctx_error = 0;
static int tmp_ctx_error_handler(Display *dpy, XErrorEvent *ev)
{
	tmp_ctx_error = 1;
	return 0;
}


int main(int argc, char** argv)
{
	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "XOpenDisplay failed");
		exit(EXIT_FAILURE);
	}

	{
		int major = -1;
		int minor = -1;
		Bool success = glXQueryVersion(dpy, &major, &minor);
		if (success == False || major < 1 || (major == 1 && minor < 3)) {
			fprintf(stderr, "invalid glx version, major=%d, minor=%d\n", major, minor);
			exit(EXIT_FAILURE);
		}
	}

	/* find visual */
	vis = NULL;
	GLXFBConfig fb_config = NULL;
	{
		static int attrs[] = {
			GLX_X_RENDERABLE    , True,
			GLX_DRAWABLE_TYPE   , GLX_WINDOW_BIT,
			GLX_RENDER_TYPE     , GLX_RGBA_BIT,
			GLX_X_VISUAL_TYPE   , GLX_TRUE_COLOR,
			GLX_RED_SIZE        , 8,
			GLX_GREEN_SIZE      , 8,
			GLX_BLUE_SIZE       , 8,
			GLX_ALPHA_SIZE      , 8,
			GLX_DEPTH_SIZE      , 0,
			GLX_STENCIL_SIZE    , 8,
			GLX_DOUBLEBUFFER    , True,
			GLX_SAMPLE_BUFFERS  , 0,
			GLX_SAMPLES         , 0,
			None
		};

		int n;
		GLXFBConfig* cs = glXChooseFBConfig(dpy, XDefaultScreen(dpy), attrs, &n);
		if (cs == NULL) {
			fprintf(stderr, "glXChooseFBConfig failed\n");
			exit(1);
		}

		int first_valid = -1;

		for (int i = 0; i < n; i++) {
			XVisualInfo* try_vis = glXGetVisualFromFBConfig(dpy, cs[i]);
			if (try_vis) {
				if (first_valid == -1) first_valid = i;

				#define REJECT(name) \
					{ \
						int value = 0; \
						if (glXGetFBConfigAttrib(dpy, cs[i], name, &value) != Success) { \
							fprintf(stderr, "glXGetFBConfigAttrib failed for " #name " \n"); \
							exit(1); \
						} \
						if (value > 0) { \
							XFree(try_vis); \
							continue; \
						} \
					}
				REJECT(GLX_SAMPLE_BUFFERS);
				REJECT(GLX_SAMPLES);
				REJECT(GLX_ACCUM_RED_SIZE);
				REJECT(GLX_ACCUM_GREEN_SIZE);
				REJECT(GLX_ACCUM_BLUE_SIZE);
				REJECT(GLX_ACCUM_ALPHA_SIZE);
				#undef REJECT

				// not rejected? pick it!
				vis = try_vis;
				fb_config = cs[i];
				break;
			}
		}

		if (vis == NULL) {
			if (first_valid == -1) {
				fprintf(stderr, "found no visual\n");
				exit(1);
			} else {
				vis = glXGetVisualFromFBConfig(dpy, cs[first_valid]);
				fb_config = cs[first_valid];
			}
		}

		XFree(cs);
	}

	/* create gl context */
	ctx = 0;
	{
		PFNGLXCREATECONTEXTATTRIBSARBPROC create_context =
			(PFNGLXCREATECONTEXTATTRIBSARBPROC)
			glXGetProcAddressARB((const GLubyte*)"glXCreateContextAttribsARB");
		if (!create_context) {
			fprintf(stderr, "failed to get proc address for glXCreateContextAttribsARB\n");
			exit(1);
		}

		const char *extensions = glXQueryExtensionsString(
			dpy,
			DefaultScreen(dpy));

		if (!is_extension_supported(extensions, "GLX_ARB_create_context")) {
			fprintf(stderr, "GLX_ARB_create_context not supported\n");
			exit(1);
		}

		int (*old_handler)(Display*, XErrorEvent*) = XSetErrorHandler(&tmp_ctx_error_handler);

		int attrs[] = {
			GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
			GLX_CONTEXT_MINOR_VERSION_ARB, 0,
			None
		};

		ctx = create_context(
			dpy,
			fb_config,
			0,
			True,
			attrs);

		XSync(dpy, False);

		if (!ctx || tmp_ctx_error) {
			fprintf(stderr, "could not create opengl context\n");
			exit(1);
		}

		XSetErrorHandler(old_handler);
	}


	int exit_status = ui_main(argc, argv);

	XCloseDisplay(dpy);

	return exit_status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

