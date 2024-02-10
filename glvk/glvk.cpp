#include "glvk.h"
#include <vector>
#include <limits>
#include <string>
#include <cstring>
#include <sstream>

#ifdef GLVK_APPLE
	#include <TargetConditionals.h>
	
	#if defined(TARGET_OS_IPHONE)
		#define GLVK_IPHONE 1
	#endif
	
	#if defined(TARGET_OS_MAC)
		#define GLVK_APPLE 1
	#endif
	
	#if defined(TARGET_OS_UNIX)
		#define GLVK_APPLE_UNIX 1
	#endif
	
	#if defined(TARGET_OS_EMBEDDED)
		#define GLVK_APPLE_EMBEDDED 1
	#endif
#endif

#ifdef GLVK_WINDOWS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#define SURFACE_EXTENSION_NAME VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#elif GLVK_LINUX
#define VK_USE_PLATFORM_XLIB_KHR
#define SURFACE_EXTENSION_NAME VK_KHR_XLIB_SURFACE_EXTENSION_NAME
#elif GLVK_APPLE
#define VK_USE_PLATFORM_MACOS_MVK
#define SURFACE_EXTENSION_NAME "VK_MVK_macos_surface"
#endif

#include <vulkan/vulkan.h>

#define GLVKDEBUG(type, severity, message) if (state.is_debug && state.debugfunc) { state.debugfunc(message, type, severity); }
#define GLVKDEBUGF(type, severity, fmt, ...) if (state.is_debug && state.debugfunc) { debugFunc(type, severity, fmt, __VA_ARGS__); }

struct GLVKvkinfo {
	VkApplicationInfo app;
};

struct GLVKvkqueuefamilies {
	uint32_t graphics;
	uint32_t present;
};

struct GLVKvkstate {
	GLVKvkinfo info;
	GLVKvkqueuefamilies queue_families;

	VkAllocationCallbacks* allocator;
	VkInstance instance;
	VkSurfaceKHR surface;
	VkSurfaceCapabilitiesKHR surface_capabilities;
	VkPhysicalDevice physical;
	VkDevice device;

	VkSurfaceFormatKHR surface_format;
	VkPresentModeKHR surface_mode;
	VkExtent2D extent;
	VkSwapchainKHR swapchain;
} static vkstate;

struct GLVKstate {
	bool inited;

	bool is_debug;
	GLVKdebugfunc debugfunc;
	GLVKwindow window;
} static state;

struct layer_t {
	const char* name;
	bool required;
};

typedef layer_t extension_t;

static void debugFuncConcat(std::stringstream& stream, std::string& format) {
	stream << format;
}

template<typename T, typename... Types>
static void debugFuncConcat(std::stringstream& stream, std::string& format, T& arg, Types&... args) {
	size_t index = format.find("{}");
	if (index == std::string::npos) {
		stream << format;
		return;
	}

	stream << std::string(format.begin(), format.begin() + index);
	stream << arg;
	if (index + 2 < format.length()) {
		format = std::string(format.begin() + index + 2, format.end());
		debugFuncConcat(stream, format, args...);
	}
}

template<typename... Types>
static void debugFunc(GLVKmessagetype type, GLVKmessageseverity severity, const char* format, Types&... types) {
	std::string fmt = format;
	std::stringstream stream;

	debugFuncConcat(stream, fmt, types...);

	state.debugfunc(stream.str().c_str(), type, severity);
}

void glvkRegisterDebugFunc(GLVKdebugfunc func) {
	if (func == nullptr) {
		return;
	}

	state.debugfunc = func;
}

void glvkSetDebug(int is_debug) {
	state.is_debug = (is_debug != 0);
}

int glvkInit(GLVKwindow window) {
	GLVKDEBUG(GLVK_TYPE_GLVK, GLVK_SEVERITY_VERBOSE, "Initialization started");
	if (state.inited) {
		GLVKDEBUG(GLVK_TYPE_GLVK, GLVK_SEVERITY_WARNING, "glvk already initialized");
		return 0;
	}
	state.inited = true;
	state.window = window;

	vkstate.info.app = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pNext = nullptr,
		.pApplicationName = "glvk",
		.applicationVersion = VK_MAKE_VERSION(1, 0, 0),
		.pEngineName = "glvk",
		.engineVersion = VK_MAKE_VERSION(1, 0, 0),
		.apiVersion = VK_API_VERSION_1_3,
	};

	std::vector<layer_t> requested_instance_layers;
	std::vector<extension_t> requested_instance_extensions = {
		{ VK_KHR_SURFACE_EXTENSION_NAME, true },
		{ SURFACE_EXTENSION_NAME, true },
	};

	if (state.is_debug) {
		requested_instance_layers.push_back({ "VK_LAYER_KHRONOS_validation", false });
		requested_instance_extensions.push_back({ VK_EXT_DEBUG_UTILS_EXTENSION_NAME, false });
	}

	uint32_t instance_layer_count = 0;
	vkEnumerateInstanceLayerProperties(&instance_layer_count, nullptr);

	std::vector<VkLayerProperties> layer_props(instance_layer_count);
	vkEnumerateInstanceLayerProperties(&instance_layer_count, layer_props.data());

	std::vector<const char*> instance_layers;
	for (const layer_t& layer : requested_instance_layers) {
		bool found = false;
		for (const VkLayerProperties& prop : layer_props) {
			if (strcmp(prop.layerName, layer.name) == 0) {
				found = true;
				instance_layers.push_back(layer.name);
			}
		}

		if (layer.required && !found) {
			GLVKDEBUGF(GLVK_TYPE_GLVK, GLVK_SEVERITY_ERROR, "Failed to find required Vulkan instance layer ", layer.name);
			return 1;
		} else if (!found) {
			GLVKDEBUGF(GLVK_TYPE_GLVK, GLVK_SEVERITY_INFO, "Failed to find Vulkan instance layer ", layer.name);
		}
	}

	uint32_t instance_extension_count = 0;
	vkEnumerateInstanceExtensionProperties(nullptr, &instance_extension_count, nullptr);

	std::vector<VkExtensionProperties> extension_props(instance_extension_count);
	vkEnumerateInstanceExtensionProperties(nullptr, &instance_extension_count, extension_props.data());

	std::vector<const char*> instance_extensions;
	for (const extension_t& extension : requested_instance_extensions) {
		bool found = false;
		for (const VkExtensionProperties& prop : extension_props) {
			if (strcmp(prop.extensionName, extension.name) == 0) {
				found = true;
				instance_extensions.push_back(extension.name);
			}
		}

		if (extension.required && !found) {
			GLVKDEBUGF(GLVK_TYPE_GLVK, GLVK_SEVERITY_ERROR, "Failed to find required Vulkan instance extension ", extension.name);
			return 1;
		} else if (!found) {
			GLVKDEBUGF(GLVK_TYPE_GLVK, GLVK_SEVERITY_INFO, "Failed to find Vulkan instance extension ", extension.name);
		}
	}

	VkInstanceCreateInfo instance_create_info = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.pApplicationInfo = &vkstate.info.app,
		.enabledLayerCount = static_cast<uint32_t>(instance_layers.size()),
		.ppEnabledLayerNames = instance_layers.data(),
		.enabledExtensionCount = static_cast<uint32_t>(instance_extensions.size()),
		.ppEnabledExtensionNames = instance_extensions.data(),
	};

	if (vkCreateInstance(&instance_create_info, vkstate.allocator, &vkstate.instance) != VK_SUCCESS) {
		GLVKDEBUG(GLVK_TYPE_VULKAN, GLVK_SEVERITY_ERROR, "Failed to create Vulkan instance");
		return 1;
	}

	#ifdef GLVK_WINDOWS
	if (window.hwnd == nullptr) {
		GLVKDEBUG(GLVK_TYPE_GLVK, GLVK_SEVERITY_ERROR, "Window handle is null");
		return 1;
	}
	if (window.hinstance == nullptr) {
		window.hinstance = GetModuleHandle(nullptr);
	}

	VkWin32SurfaceCreateInfoKHR surface_create_info = {
		.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
		.pNext = nullptr,
		.flags = 0,
		.hinstance = reinterpret_cast<HINSTANCE>(window.hinstance),
		.hwnd = reinterpret_cast<HWND>(window.hwnd),
	};

	if (vkCreateWin32SurfaceKHR(vkstate.instance, &surface_create_info, vkstate.allocator, &vkstate.surface) != VK_SUCCESS) {
		GLVKDEBUG(GLVK_TYPE_VULKAN, GLVK_SEVERITY_ERROR, "Failed to create Vulkan surface");
		return 1;
	}
	#elif GLVK_LINUX
	if (window.display == nullptr) {
		GLVKDEBUG(GLVK_TYPE_GLVK, GLVK_SEVERITY_ERROR, "Display is null");
		return 1;
	}
	if (window.window == 0) {
		GLVKDEBUG(GLVK_TYPE_GLVK, GLVK_SEVERITY_ERROR, "Window is null");
		return 1;
	}

	VkXlibSurfaceCreateInfoKHR surface_create_info = {
		.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
		.pNext = nullptr,
		.flags = 0,
		.dpy = reinterpret_cast<Display*>(window.display),
		.window = static_cast<Window>(window.window),
	};

	if (vkCreateXlibSurfaceKHR(vkstate.instance, &surface_create_info, vkstate.allocator, &vkstate.surface) != VK_SUCCESS) {
		GLVKDEBUG(GLVK_TYPE_VULKAN, GLVK_SEVERITY_ERROR, "Failed to create Vulkan surface");
		return 1;
	}
	#elif GLVK_APPLE
	if (window.view == nullptr) {
		GLVKDEBUG(GLVK_TYPE_GLVK, GLVK_SEVERITY_ERROR, "View is null");
		return 1;
	}

	VkMacOSSurfaceCreateInfoMVK surface_create_info = {
		.sType = VK_STRUCTURE_TYPE_MACOS_SURFACE_CREATE_INFO_MVK,
		.pNext = nullptr,
		.flags = 0,
		.pView = window.view,
	};

	if (vkCreateMacOSSurfaceMVK(vkstate.instance, &surface_create_info, vkstate.allocator, &vkstate.surface) != VK_SUCCESS) {
		GLVKDEBUG(GLVK_TYPE_VULKAN, GLVK_SEVERITY_ERROR, "Failed to create Vulkan surface");
		return 1;
	}
	#endif

	uint32_t physical_count = 0;
	vkEnumeratePhysicalDevices(vkstate.instance, &physical_count, nullptr);

	std::vector<VkPhysicalDevice> physicals(physical_count);
	vkEnumeratePhysicalDevices(vkstate.instance, &physical_count, physicals.data());

	VkPhysicalDeviceProperties physical_props;
	VkPhysicalDeviceFeatures physical_features;
	VkPhysicalDeviceMemoryProperties physical_mem_props;

	{
		GLVKDEBUG(GLVK_TYPE_VULKAN, GLVK_SEVERITY_VERBOSE, "Physical devices available:");

		size_t best = 0;
		size_t best_index = std::numeric_limits<size_t>::max();
		for (size_t i = 0; i < physicals.size(); ++i) {
			vkGetPhysicalDeviceProperties(physicals[i], &physical_props);
			vkGetPhysicalDeviceFeatures(physicals[i], &physical_features);
			vkGetPhysicalDeviceMemoryProperties(physicals[i], &physical_mem_props);

			size_t score = 0;
			if (physical_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
				score += 10000;
			}

			score += physical_props.limits.maxImageDimension2D;
			score += static_cast<size_t>(physical_props.limits.maxFramebufferWidth * physical_props.limits.maxFramebufferHeight) * 0.01f;

			if (score > best) {
				best = score;
				best_index = i;
			}

			const char* const device_types[VK_PHYSICAL_DEVICE_TYPE_CPU + 1] = {
				"Other",
				"Integrated",
				"Discrete",
				"Virtual",
				"CPU",
			};

			GLVKDEBUGF(GLVK_TYPE_VULKAN, GLVK_SEVERITY_VERBOSE, "    [{}] ({}) {}", device_types[physical_props.deviceType], score, physical_props.deviceName);
		}

		if (best_index == std::numeric_limits<size_t>::max()) {
			GLVKDEBUG(GLVK_TYPE_GLVK, GLVK_SEVERITY_ERROR, "Failed to find GPU");
			return 1;
		}

		vkstate.physical = physicals[best_index];
	}

	if (vkstate.physical == VK_NULL_HANDLE) {
		GLVKDEBUG(GLVK_TYPE_GLVK, GLVK_SEVERITY_ERROR, "Failed to find GPU");
		return 1;
	}
	vkGetPhysicalDeviceProperties(vkstate.physical, &physical_props);
	vkGetPhysicalDeviceFeatures(vkstate.physical, &physical_features);
	vkGetPhysicalDeviceMemoryProperties(vkstate.physical, &physical_mem_props);
	GLVKDEBUGF(GLVK_TYPE_VULKAN, GLVK_SEVERITY_VERBOSE, "Found GPU \"{}\"", physical_props.deviceName);

	std::vector<layer_t> requested_device_layers;
	std::vector<extension_t> requested_device_extensions = {
		{ VK_KHR_SWAPCHAIN_EXTENSION_NAME, true },
	};

	uint32_t device_layer_count = 0;
	vkEnumerateDeviceLayerProperties(vkstate.physical, &device_layer_count, nullptr);

	std::vector<VkLayerProperties> device_layer_props(device_layer_count);
	vkEnumerateDeviceLayerProperties(vkstate.physical, &device_layer_count, device_layer_props.data());

	std::vector<const char*> device_layer_names;
	for (layer_t& layer : requested_device_layers) {
		bool found = false;
		for (VkLayerProperties& prop : device_layer_props) {
			GLVKDEBUG(GLVK_TYPE_GLVK, GLVK_SEVERITY_INFO, prop.layerName);
			if (strcmp(prop.layerName, layer.name) == 0) {
				found = true;
				device_layer_names.push_back(layer.name);
				break;
			}
		}

		if (!found) {
			if (layer.required) {
				GLVKDEBUGF(GLVK_TYPE_GLVK, GLVK_SEVERITY_ERROR, "Failed to find required Vulkan device layer ", layer.name);
				return 1;
			} else {
				GLVKDEBUGF(GLVK_TYPE_GLVK, GLVK_SEVERITY_DEBUG, "Failed to find Vulkan device layer ", layer.name);
			}
		}
	}

	uint32_t device_extension_count = 0;
	vkEnumerateDeviceExtensionProperties(vkstate.physical, nullptr, &device_extension_count, nullptr);

	std::vector<VkExtensionProperties> device_extension_props(device_extension_count);
	vkEnumerateDeviceExtensionProperties(vkstate.physical, nullptr, &device_extension_count, device_extension_props.data());

	std::vector<const char*> device_extension_names;
	for (extension_t& extension : requested_device_extensions) {
		bool found = false;
		for (VkExtensionProperties& prop : device_extension_props) {
			if (strcmp(prop.extensionName, extension.name) == 0) {
				found = true;
				device_extension_names.push_back(extension.name);
				break;
			}
		}

		if (!found) {
			if (extension.required) {
				GLVKDEBUGF(GLVK_TYPE_GLVK, GLVK_SEVERITY_ERROR, "Failed to find required Vulkan device extension ", extension.name);
				return 1;
			} else {
				GLVKDEBUGF(GLVK_TYPE_GLVK, GLVK_SEVERITY_INFO, "Failed to find Vulkan device extension ", extension.name);
			}
		}
	}

	uint32_t queue_family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(vkstate.physical, &queue_family_count, nullptr);

	std::vector<VkQueueFamilyProperties> queue_family_props(queue_family_count);
	vkGetPhysicalDeviceQueueFamilyProperties(vkstate.physical, &queue_family_count, queue_family_props.data());

	uint32_t gfx = std::numeric_limits<uint32_t>::max();
	uint32_t prs = std::numeric_limits<uint32_t>::max();
	for (uint32_t i = 0; i < queue_family_count; ++i) {
		if (queue_family_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			gfx = i;
		}

		VkBool32 present = VK_FALSE;
		vkGetPhysicalDeviceSurfaceSupportKHR(vkstate.physical, i, vkstate.surface, &present);
		if (present == VK_TRUE) {
			prs = i;
		}

		if (gfx != std::numeric_limits<uint32_t>::max() && prs != std::numeric_limits<uint32_t>::max()) {
			break;
		}
	}

	if (gfx == std::numeric_limits<uint32_t>::max()) {
		GLVKDEBUG(GLVK_TYPE_GLVK, GLVK_SEVERITY_ERROR, "Failed to find graphics queue family");
		return 1;
	}
	if (prs == std::numeric_limits<uint32_t>::max()) {
		GLVKDEBUG(GLVK_TYPE_GLVK, GLVK_SEVERITY_ERROR, "Failed to find present queue family");
		return 1;
	}

	vkstate.queue_families = {
		.graphics = gfx,
		.present = prs,
	};

	uint32_t queue_families[] = {
		vkstate.queue_families.graphics,
		vkstate.queue_families.present,
	};

	float priority = 1.0f;
	std::vector<VkDeviceQueueCreateInfo> queue_create_infos;

	for (size_t i = 0; i < 2; ++i) {
		VkDeviceQueueCreateInfo qcreate_info;
		for (size_t j = 0; j < queue_create_infos.size(); ++j) {
			if (queue_create_infos[j].queueFamilyIndex == queue_families[i]) {
				goto next;
			}
		}

		qcreate_info = {
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.queueFamilyIndex = queue_families[i],
			.queueCount = 1,
			.pQueuePriorities = &priority,
		};

		queue_create_infos.push_back(qcreate_info);
	next:;
	}

	VkDeviceCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size()),
		.pQueueCreateInfos = queue_create_infos.data(),
		.enabledLayerCount = static_cast<uint32_t>(device_layer_names.size()),
		.ppEnabledLayerNames = device_layer_names.data(),
		.enabledExtensionCount = static_cast<uint32_t>(device_extension_names.size()),
		.ppEnabledExtensionNames = device_extension_names.data(),
		.pEnabledFeatures = nullptr,
	};

	if (vkCreateDevice(vkstate.physical, &create_info, vkstate.allocator, &vkstate.device) != VK_SUCCESS) {
		GLVKDEBUG(GLVK_TYPE_VULKAN, GLVK_SEVERITY_ERROR, "Failed to create Vulkan device");
		return 1;
	}

	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vkstate.physical, vkstate.surface, &vkstate.surface_capabilities);

	uint32_t surface_format_count = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(vkstate.physical, vkstate.surface, &surface_format_count, nullptr);
	if (surface_format_count == 0) {
		GLVKDEBUG(GLVK_TYPE_GLVK, GLVK_SEVERITY_ERROR, "Failed to find surface formats");
		return 1;
	}

	std::vector<VkSurfaceFormatKHR> surface_formats(surface_format_count);
	vkGetPhysicalDeviceSurfaceFormatsKHR(vkstate.physical, vkstate.surface, &surface_format_count, surface_formats.data());

	uint32_t surface_format_index = std::numeric_limits<uint32_t>::max();
	for (size_t i = 0; i < surface_formats.size(); ++i) {
		if (surface_formats[i].format == VK_FORMAT_B8G8R8A8_SRGB && surface_formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			surface_format_index = i;
			break;
		}
	}

	if (surface_format_count == std::numeric_limits<uint32_t>::max()) {
		GLVKDEBUG(GLVK_TYPE_GLVK, GLVK_SEVERITY_ERROR, "Failed to find surface format");
		return 1;
	}

	uint32_t surface_modes_count = 0;
	vkGetPhysicalDeviceSurfacePresentModesKHR(vkstate.physical, vkstate.surface, &surface_modes_count, nullptr);
	if (surface_modes_count == 0) {
		GLVKDEBUG(GLVK_TYPE_GLVK, GLVK_SEVERITY_ERROR, "Failed to find surface present modes");
		return 1;
	}

	std::vector<VkPresentModeKHR> surface_modes(surface_modes_count);
	vkGetPhysicalDeviceSurfacePresentModesKHR(vkstate.physical, vkstate.surface, &surface_modes_count, surface_modes.data());

	uint32_t surface_mode_index = std::numeric_limits<uint32_t>::max();
	for (size_t i = 0; i < surface_modes.size(); ++i) {
		if (surface_modes[i] == VK_PRESENT_MODE_FIFO_KHR) {
			surface_mode_index = i;
		}
		if (surface_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
			surface_mode_index = i;
			break;
		}
	}

	if (surface_mode_index == std::numeric_limits<uint32_t>::max()) {
		GLVKDEBUG(GLVK_TYPE_GLVK, GLVK_SEVERITY_ERROR, "Failed to find surface present mode");
		return 1;
	}

	vkstate.surface_format = surface_formats[surface_format_index];
	vkstate.surface_mode = surface_modes[surface_mode_index];
	vkstate.extent = vkstate.surface_capabilities.currentExtent;

	VkSwapchainCreateInfoKHR swapchain_create_info = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.pNext = nullptr,
		.flags = 0,
		.surface = vkstate.surface,
		.minImageCount = vkstate.surface_capabilities.minImageCount,
		.imageFormat = vkstate.surface_format.format,
		.imageColorSpace = vkstate.surface_format.colorSpace,
		.imageExtent = vkstate.extent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices = nullptr,
		.preTransform = vkstate.surface_capabilities.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = vkstate.surface_mode,
		.clipped = VK_TRUE,
		.oldSwapchain = VK_NULL_HANDLE,
	};

	uint32_t indices[2] = { vkstate.queue_families.graphics, vkstate.queue_families.present };
	if (vkstate.queue_families.graphics != vkstate.queue_families.present) {
		swapchain_create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		swapchain_create_info.queueFamilyIndexCount = 2;
		swapchain_create_info.pQueueFamilyIndices = indices;
	}

	if (vkCreateSwapchainKHR(vkstate.device, &swapchain_create_info, vkstate.allocator, &vkstate.swapchain) != VK_SUCCESS) {
		GLVKDEBUG(GLVK_TYPE_VULKAN, GLVK_SEVERITY_ERROR, "Failed to create Vulkan swapchain");
		return 1;
	}

	GLVKDEBUG(GLVK_TYPE_GLVK, GLVK_SEVERITY_VERBOSE, "Initialization success");
	return 0;
}

void glvkDeinit() {
	if (!state.inited) {
		return;
	}
	state.inited = false;

	vkDestroySwapchainKHR(vkstate.device, vkstate.swapchain, vkstate.allocator);
	vkDestroyDevice(vkstate.device, vkstate.allocator);
	vkDestroySurfaceKHR(vkstate.instance, vkstate.surface, vkstate.allocator);
	vkDestroyInstance(vkstate.instance, nullptr);
}
