#include "glvk_gh.h"

#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

GLVKwindow glvkGetGLFWWindowGH(GLFWwindow* window) {
	return (GLVKwindow) {
		.display = glfwGetX11Display(),
		.window = glfwGetX11Window(window),
	};
}

