#include "glvk_gh.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <GLFW/glfw3.h>

GLVKwindow glvkGetGLFWWindowGH(GLFWwindow* window) {
	GLVKwindow w = {
		.hinstance = GetModuleHandle(NULL),
		.hwnd = glfwGetWin32Window(window),
	};

	return w;
}
