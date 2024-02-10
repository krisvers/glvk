#ifndef KRISVERS_GLVK_GLFWHELPER_H
#define KRISVERS_GLVK_GLFWHELPER_H

#include "../glvk/glvk.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(__cplusplus) || defined(__OBJC__)
extern "C" {
#endif

/* returns a GLVKwindow with proper members for the current platform from a GLFWwindow */
GLVKwindow glvkGetGLFWWindowGH(GLFWwindow* window);

#if defined(__cplusplus) || defined(__OBJC__)
}
#endif

#endif
