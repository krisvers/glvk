#include "glvk/glvk.h"
#include "glvk_gh/glvk_gh.h"
#include <stdio.h>

#include <GLFW/glfw3.h>

void glvk_debug(const char* message, GLVKmessagetype type, GLVKmessageseverity severity) {
	const char* const types[] = {
		"glvk",
		"opengl",
		"vulkan",
		"unknown",
	};

	const char* const severities[] = {
		"verbose",
		"debug",
		"info",
		"warning",
		"error",
		"unknown",
	};

	if (type < 0 || type > GLVK_TYPE_LAST) {
		type = GLVK_TYPE_UNKNOWN;
	}

	if (severity < GLVK_SEVERITY_VERBOSE || severity > GLVK_SEVERITY_LAST) {
		severity = GLVK_SEVERITY_UNKNOWN;
	}

	printf("[%s] (%s) %s\n", types[type], severities[severity + 2], message);
}

int main(int argc, char** argv) {
	if (!glfwInit()) {
		printf("Failed to initialize GLFW\n");
		return 1;
	}

	GLFWwindow* window = glfwCreateWindow(800, 600, "test", NULL, NULL);
	if (window == NULL) {
		printf("Failed to create window\n");
		glfwTerminate();
		return 1;
	}

	glvkSetDebug(1);
	glvkRegisterDebugFunc(glvk_debug);

	GLVKwindow glvk_window = glvkGetGLFWWindowGH(window);

	if (glvkInit(glvk_window) != 0) {
		printf("Failed to initialize glvk\n");
		return 1;
	}

	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
		glfwSwapBuffers(window);
	}

	glvkDeinit();
	glfwTerminate();
	return 0;
}
