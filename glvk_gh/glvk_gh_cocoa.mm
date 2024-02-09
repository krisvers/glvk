#include "glvk_gh.h"

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#include <GLFW/glfw3.h>

GLVKwindow glvkGetGLFWWindowGH(GLFWwindow* window) {
	return (GLVKwindow) {
		.view = [((NSWindow*) glfwGetCocoaWindow(window)) contentView],
	};
}
