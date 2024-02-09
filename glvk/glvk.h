#ifndef KRISVERS_GLVK_H
#define KRISVERS_GLVK_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(_WIN64) || defined(__WIN32__) || defined(_MSC_VER)
#define GLVK_WINDOWS 1
#endif

#if defined(__linux__)
#define GLVK_LINUX 1
#endif

#if defined(__unix__)
#define GLVK_UNIX 1
#endif

#if defined(__posix__)
#define GLVK_POSIX 1
#endif

#if defined(__APPLE__)
#define GLVK_APPLE 1
#endif

typedef enum {
	GLVK_TYPE_GLVK = 0,
	GLVK_TYPE_OPENGL,
	GLVK_TYPE_VULKAN,
	GLVK_TYPE_UNKNOWN,
	GLVK_TYPE_LAST = GLVK_TYPE_VULKAN,
} GLVKmessagetype;

typedef enum {
	GLVK_SEVERITY_VERBOSE = -2,
	GLVK_SEVERITY_DEBUG = -1,
	GLVK_SEVERITY_INFO = 0,
	GLVK_SEVERITY_WARNING,
	GLVK_SEVERITY_ERROR,
	GLVK_SEVERITY_UNKNOWN,
	GLVK_SEVERITY_LAST = GLVK_SEVERITY_ERROR,
} GLVKmessageseverity;

typedef struct {
#ifdef GLVK_WINDOWS
	void* hinstance;
	void* hwnd;
#elif GLVK_LINUX
	void* display;
	unsigned long window;
#elif GLVK_APPLE
	void* view;
#endif
} GLVKwindow;

typedef void (*GLVKdebugfunc)(const char* message, GLVKmessagetype type, GLVKmessageseverity severity);

/* initializes all necessary vulkan utilities */
int glvkInit(GLVKwindow window);

/* registers debug output callback */
void glvkRegisterDebugFunc(GLVKdebugfunc func);

/* enables or disables debugging */
void glvkSetDebug(int enabled);

/* cleans up all necessary vulkan utilities*/
void glvkDeinit();

#ifdef __cplusplus
}
#endif

#endif
