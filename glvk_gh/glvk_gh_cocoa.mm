#include "glvk_gh.h"

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#include <GLFW/glfw3.h>

#include <QuartzCore/CAMetalLayer.h>

GLVKwindow glvkGetGLFWWindowGH(GLFWwindow* window) {
	NSBundle* bundle = [NSBundle bundleWithPath:@"/System/Library/Frameworks/QuartzCore.framework"];
	if (bundle == nullptr) {
		return (GLVKwindow) { .layer = nullptr };
	}

	CAMetalLayer* layer = [[bundle classNamed:@"CAMetalLayer"] layer];
	return (GLVKwindow) {
		.layer = layer,
	};
}
