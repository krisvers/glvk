#include "glvk.h"
#include <vector>
#include <limits>
#include <string>
#include <cstring>
#include <sstream>
#include <fstream>
#include <stack>
#include <vulkan/vulkan_core.h>

#ifdef GLVK_APPLE
	#include <TargetConditionals.h>
	
	#if defined(TARGET_OS_IPHONE)
		#define GLVK_IPHONE 1
	#endif
	
	#if defined(TARGET_OS_MAC)
		#define GLVK_MAC 1
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
#define SURFACE_EXTENSION_NAME "VK_EXT_metal_surface"
#define VK_USE_PLATFORM_METAL_EXT
#endif

#include <vulkan/vulkan.h>

#define GLVKDEBUG(type, severity, message) if (state.is_debug && state.debugfunc) { state.debugfunc(message, type, severity); }
#define GLVKDEBUGF(type, severity, fmt, ...) if (state.is_debug && state.debugfunc) { debugFunc(type, severity, fmt, __VA_ARGS__); }
#define GLPUSHERROR(error) { GLVKDEBUGF(GLVK_TYPE_OPENGL, GLVK_SEVERITY_ERROR, "OpenGL error: {} at {}:{}", (error == GL_NO_ERROR) ? "GL_NO_ERROR" : (error == GL_INVALID_ENUM) ? "GL_INVALID_ENUM" : (error == GL_INVALID_VALUE) ? "GL_INVALID_VALUE" : (error == GL_INVALID_OPERATION) ? "GL_INVALID_OPERATION" : (error == GL_STACK_OVERFLOW) ? "GL_STACK_OVERFLOW" : (error == GL_STACK_UNDERFLOW) ? "GL_STACK_UNDERFLOW" : (error == GL_OUT_OF_MEMORY) ? "GL_OUT_OF_MEMORY" : "unknown", __FILE__, __LINE__); glPushError(error); }

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
	VkViewport viewport;
	VkRect2D scissor;

	uint32_t swapchain_image_count;
	std::vector<VkImage> swapchain_images;
	std::vector<VkImageView> swapchain_views;
	VkSwapchainKHR swapchain;

	std::vector<VkFramebuffer> framebuffers;

	VkRenderPass render_pass;
	VkShaderModule vshader;
	VkShaderModule fshader;

	VkDescriptorSetLayout desc_layout;
	VkPipelineLayout pipeline_layout;
	VkPipeline pipeline;

	VkCommandBuffer command_buffer;
	VkCommandPool command_pool;

	VkQueue graphics_queue;
	VkQueue present_queue;
	VkSemaphore image_available;
	VkSemaphore render_finished;
	VkFence in_flight_fence;

	VkDebugUtilsMessengerEXT debug_messenger;
} static vkstate;

struct glbuffer_t {
	GLuint id;
	VkBuffer buffer;
	VkDeviceMemory memory;
	VkDeviceSize size;
	GLenum usage;
};

struct GLVKglboundbuffers {
	GLuint array;
	GLuint element_array;
	GLuint copy_read;
	GLuint copy_write;
	GLuint pixel_pack;
	GLuint pixel_unpack;
	GLuint transform_feedback;
	GLuint uniform;
	GLuint shader_storage;
	GLuint texture;
};

struct GLVKglstate {
	std::stack<GLenum> errors;
	std::vector<glbuffer_t> buffers;

	GLVKglboundbuffers bound_buffers;
	GLuint bound_vao;
} static glstate;

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

struct vertex_t {
	float pos[3];
};

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
	if (index + 2 < format.size()) {
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

static void glPushError(GLenum error) {
	if (glstate.errors.size() > 64) {
		glstate.errors.pop();
	}
	glstate.errors.push(error);
}

int glvkInit(GLVKwindow window) {
	GLVKDEBUG(GLVK_TYPE_GLVK, GLVK_SEVERITY_VERBOSE, "Initialization started");
	if (state.inited) {
		GLVKDEBUG(GLVK_TYPE_GLVK, GLVK_SEVERITY_WARNING, "glvk already initialized");
		return 0;
	}
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

	#ifdef GLVK_MAC

	#endif

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
			GLVKDEBUGF(GLVK_TYPE_GLVK, GLVK_SEVERITY_ERROR, "Failed to find required Vulkan instance layer {}", layer.name);
			return 1;
		} else if (!found) {
			GLVKDEBUGF(GLVK_TYPE_GLVK, GLVK_SEVERITY_INFO, "Failed to find Vulkan instance layer {}", layer.name);
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
			GLVKDEBUGF(GLVK_TYPE_GLVK, GLVK_SEVERITY_ERROR, "Failed to find required Vulkan instance extension {}", extension.name);
			return 1;
		} else if (!found) {
			GLVKDEBUGF(GLVK_TYPE_GLVK, GLVK_SEVERITY_INFO, "Failed to find Vulkan instance extension {}", extension.name);
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

	if (state.is_debug) {
		for (const char*& name : instance_extensions) {
			if (strcmp(name, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0) {
				PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(vkstate.instance, "vkCreateDebugUtilsMessengerEXT"));
				if (vkCreateDebugUtilsMessengerEXT == nullptr) {
					GLVKDEBUG(GLVK_TYPE_VULKAN, GLVK_SEVERITY_WARNING, "Failed to find vkCreateDebugUtilsMessengerEXT");
					break;
				}

				VkDebugUtilsMessengerCreateInfoEXT debug_create_info = {
					.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
					.pNext = nullptr,
					.flags = 0,
					.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
					.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
					.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_type, const VkDebugUtilsMessengerCallbackDataEXT* callback_data, void* user_data) -> VkBool32 {
						const char* const severity = (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) ? "verbose" : (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) ? "info" : (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) ? "warning" : (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "error" : "unknown";
						const char* const type = (message_type & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) ? "general" : (message_type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) ? "performance" : (message_type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) ? "validation" : "unknown";
						GLVKDEBUGF(GLVK_TYPE_VULKAN, GLVK_SEVERITY_DEBUG, "[{}] ({}) {}", severity, type, callback_data->pMessage);

						if (message_severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
							return VK_TRUE;
						}

						return VK_FALSE;
					},
					.pUserData = nullptr,
				};

				if (vkCreateDebugUtilsMessengerEXT(vkstate.instance, &debug_create_info, vkstate.allocator, &vkstate.debug_messenger) != VK_SUCCESS) {
					GLVKDEBUG(GLVK_TYPE_VULKAN, GLVK_SEVERITY_WARNING, "Failed to create Vulkan debug messenger");
				}
				break;
			}
		}
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
	#elif GLVK_MAC
	if (window.layer == nullptr) {
		GLVKDEBUG(GLVK_TYPE_GLVK, GLVK_SEVERITY_ERROR, "layer is null");
		return 1;
	}

	VkMetalSurfaceCreateInfoEXT surface_create_info = {
		.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT,
		.pNext = nullptr,
		.flags = 0,
		.pLayer = window.layer,
	};

	if (vkCreateMetalSurfaceEXT(vkstate.instance, &surface_create_info, vkstate.allocator, &vkstate.surface) != VK_SUCCESS) {
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

	if (state.is_debug) {
		requested_device_layers.push_back({ "VK_LAYER_KHRONOS_validation", false });
	}

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

	vkGetDeviceQueue(vkstate.device, vkstate.queue_families.graphics, 0, &vkstate.graphics_queue);
	vkGetDeviceQueue(vkstate.device, vkstate.queue_families.present, 0, &vkstate.present_queue);
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

	vkGetSwapchainImagesKHR(vkstate.device, vkstate.swapchain, &vkstate.swapchain_image_count, nullptr);
	vkstate.swapchain_images.resize(vkstate.swapchain_image_count);
	vkGetSwapchainImagesKHR(vkstate.device, vkstate.swapchain, &vkstate.swapchain_image_count, vkstate.swapchain_images.data());

	vkstate.swapchain_views.resize(vkstate.swapchain_image_count);
	for (size_t i = 0; i < vkstate.swapchain_image_count; ++i) {
		VkImageViewCreateInfo view_create_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.image = vkstate.swapchain_images[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = vkstate.surface_format.format,
			.components = {
				.r = VK_COMPONENT_SWIZZLE_IDENTITY,
				.g = VK_COMPONENT_SWIZZLE_IDENTITY,
				.b = VK_COMPONENT_SWIZZLE_IDENTITY,
				.a = VK_COMPONENT_SWIZZLE_IDENTITY,
			},
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		};

		vkCreateImageView(vkstate.device, &view_create_info, vkstate.allocator, &vkstate.swapchain_views[i]);
	}

	VkAttachmentDescription color_attachment = {
		.flags = 0,
		.format = vkstate.surface_format.format,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	};

	VkAttachmentReference color_reference = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};

	VkSubpassDescription subpass = {
		.flags = 0,
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.inputAttachmentCount = 0,
		.pInputAttachments = nullptr,
		.colorAttachmentCount = 1,
		.pColorAttachments = &color_reference,
		.pResolveAttachments = nullptr,
		.pDepthStencilAttachment = nullptr,
		.preserveAttachmentCount = 0,
		.pPreserveAttachments = nullptr,
	};

	VkSubpassDependency subpass_dependency = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dependencyFlags = 0,
	};

	VkRenderPassCreateInfo render_pass_create_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.attachmentCount = 1,
		.pAttachments = &color_attachment,
		.subpassCount = 1,
		.pSubpasses = &subpass,
		.dependencyCount = 1,
		.pDependencies = &subpass_dependency,
	};

	vkCreateRenderPass(vkstate.device, &render_pass_create_info, vkstate.allocator, &vkstate.render_pass);

	vkstate.framebuffers.resize(vkstate.swapchain_image_count);
	for (size_t i = 0; i < vkstate.swapchain_image_count; ++i) {
		VkFramebufferCreateInfo framebuffer_create_info = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.renderPass = vkstate.render_pass,
			.attachmentCount = 1,
			.pAttachments = &vkstate.swapchain_views[i],
			.width = vkstate.extent.width,
			.height = vkstate.extent.height,
			.layers = 1,
		};

		vkCreateFramebuffer(vkstate.device, &framebuffer_create_info, vkstate.allocator, &vkstate.framebuffers[i]);
	}
	
	std::ifstream vshader_file("assets/shaders/vert.spv", std::ios::ate | std::ios::binary);
	std::ifstream fshader_file("assets/shaders/frag.spv", std::ios::ate | std::ios::binary);
	if (!vshader_file.is_open()) {
		GLVKDEBUG(GLVK_TYPE_GLVK, GLVK_SEVERITY_ERROR, "Failed to open assets/shaders/vert.spv");
		return 1;
	}
	if (!fshader_file.is_open()) {
		GLVKDEBUG(GLVK_TYPE_GLVK, GLVK_SEVERITY_ERROR, "Failed to open assets/shaders/frag.spv");
		return 1;
	}

	size_t size = vshader_file.tellg();
	std::vector<uint8_t> v_spv(size);
	vshader_file.seekg(0);
	vshader_file.read(reinterpret_cast<char*>(v_spv.data()), size);
	vshader_file.close();

	size = fshader_file.tellg();
	std::vector<uint8_t> f_spv(size);
	fshader_file.seekg(0);
	fshader_file.read(reinterpret_cast<char*>(f_spv.data()), size);
	fshader_file.close();

	VkShaderModuleCreateInfo vshader_create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.codeSize = v_spv.size(),
		.pCode = reinterpret_cast<const uint32_t*>(v_spv.data()),
	};

	if (vkCreateShaderModule(vkstate.device, &vshader_create_info, vkstate.allocator, &vkstate.vshader) != VK_SUCCESS) {
		GLVKDEBUG(GLVK_TYPE_VULKAN, GLVK_SEVERITY_ERROR, "Failed to create vertex shader module");
		return 1;
	}

	VkShaderModuleCreateInfo fshader_create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.codeSize = f_spv.size(),
		.pCode = reinterpret_cast<const uint32_t*>(f_spv.data()),
	};

	if (vkCreateShaderModule(vkstate.device, &fshader_create_info, vkstate.allocator, &vkstate.fshader) != VK_SUCCESS) {
		GLVKDEBUG(GLVK_TYPE_VULKAN, GLVK_SEVERITY_ERROR, "Failed to create fragment shader module");
		return 1;
	}

	VkPipelineShaderStageCreateInfo shader_stage_create_infos[2] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vkstate.vshader,
			.pName = "main",
			.pSpecializationInfo = nullptr,
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = vkstate.fshader,
			.pName = "main",
			.pSpecializationInfo = nullptr,
		},
	};

	VkDynamicState dynamic_states[2] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};

	VkPipelineDynamicStateCreateInfo pipeline_ds_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.dynamicStateCount = 2,
		.pDynamicStates = dynamic_states,
	};

	/*
	VkVertexInputBindingDescription vinput_binding_desc = {
		.binding = 0,
		.stride = sizeof(vertex_t),
		.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
	};

	VkVertexInputAttributeDescription vinput_attr_desc[1] = {
		{
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(vertex_t, pos),
		},
	};
	*/

	VkPipelineVertexInputStateCreateInfo pipeline_vinput_state_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.vertexBindingDescriptionCount = 0,
		.pVertexBindingDescriptions = nullptr,
		.vertexAttributeDescriptionCount = 0,
		.pVertexAttributeDescriptions = nullptr,
	};

	VkPipelineInputAssemblyStateCreateInfo pipeline_ia_state_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.primitiveRestartEnable = VK_FALSE,
	};

	vkstate.viewport = {
		.x = 0,
		.y = 0,
		.width = static_cast<float>(vkstate.extent.width),
		.height = static_cast<float>(vkstate.extent.height),
		.minDepth = 0,
		.maxDepth = 1,
	};

	vkstate.scissor = {
		.offset = { 0, 0 },
		.extent = vkstate.extent,
	};

	VkPipelineViewportStateCreateInfo pipeline_viewport_state_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.viewportCount = 1,
		.pViewports = &vkstate.viewport,
		.scissorCount = 1,
		.pScissors = &vkstate.scissor,
	};

	VkPipelineRasterizationStateCreateInfo pipeline_rast_state_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.depthClampEnable = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.depthBiasEnable = VK_FALSE,
		.depthBiasConstantFactor = 0,
		.depthBiasClamp = 0,
		.depthBiasSlopeFactor = 0,
		.lineWidth = 1,
	};

	VkPipelineMultisampleStateCreateInfo pipeline_ms_state_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
		.sampleShadingEnable = VK_FALSE,
		.minSampleShading = 1,
		.pSampleMask = nullptr,
		.alphaToCoverageEnable = VK_FALSE,
		.alphaToOneEnable = VK_FALSE,
	};

	VkPipelineColorBlendAttachmentState pipeline_cba_state = {
		.blendEnable = VK_FALSE,
		.srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
		.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.colorBlendOp = VK_BLEND_OP_ADD,
		.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
		.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
		.alphaBlendOp = VK_BLEND_OP_ADD,
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};

	VkPipelineColorBlendStateCreateInfo pipeline_cb_state_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.logicOpEnable = VK_FALSE,
		.logicOp = VK_LOGIC_OP_COPY,
		.attachmentCount = 1,
		.pAttachments = &pipeline_cba_state,
		.blendConstants = { 0, 0, 0, 0 },
	};

	VkDescriptorSetLayoutCreateInfo desc_set_layout_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.bindingCount = 0,
		.pBindings = nullptr,
	};

	if (vkCreateDescriptorSetLayout(vkstate.device, &desc_set_layout_create_info, vkstate.allocator, &vkstate.desc_layout)) {
		GLVKDEBUG(GLVK_TYPE_VULKAN, GLVK_SEVERITY_ERROR, "Failed to create descriptor set layout");
		return 1;
	}

	VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.setLayoutCount = 1,
		.pSetLayouts = &vkstate.desc_layout,
		.pushConstantRangeCount = 0,
		.pPushConstantRanges = nullptr,
	};

	if (vkCreatePipelineLayout(vkstate.device, &pipeline_layout_create_info, vkstate.allocator, &vkstate.pipeline_layout) != VK_SUCCESS) {
		GLVKDEBUG(GLVK_TYPE_VULKAN, GLVK_SEVERITY_ERROR, "Failed to create pipeline layout");
		return 1;
	}

	VkGraphicsPipelineCreateInfo pipeline_create_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.stageCount = 2,
		.pStages = shader_stage_create_infos,
		.pVertexInputState = &pipeline_vinput_state_create_info,
		.pInputAssemblyState = &pipeline_ia_state_create_info,
		.pTessellationState = nullptr,
		.pViewportState = &pipeline_viewport_state_create_info,
		.pRasterizationState = &pipeline_rast_state_create_info,
		.pMultisampleState = &pipeline_ms_state_create_info,
		.pDepthStencilState = nullptr,
		.pColorBlendState = &pipeline_cb_state_create_info,
		.pDynamicState = &pipeline_ds_create_info,
		.layout = vkstate.pipeline_layout,
		.renderPass = vkstate.render_pass,
		.subpass = 0,
		.basePipelineHandle = VK_NULL_HANDLE,
		.basePipelineIndex = -1,
	};

	if (vkCreateGraphicsPipelines(vkstate.device, VK_NULL_HANDLE, 1, &pipeline_create_info, vkstate.allocator, &vkstate.pipeline) != VK_SUCCESS) {
		GLVKDEBUG(GLVK_TYPE_VULKAN, GLVK_SEVERITY_ERROR, "Failed to create graphics pipeline");
		return 1;
	}

	VkCommandPoolCreateInfo command_pool_create_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = vkstate.queue_families.graphics,
	};

	if (vkCreateCommandPool(vkstate.device, &command_pool_create_info, vkstate.allocator, &vkstate.command_pool) != VK_SUCCESS) {
		GLVKDEBUG(GLVK_TYPE_VULKAN, GLVK_SEVERITY_ERROR, "Failed to create command pool");
		return 1;
	}

	VkCommandBufferAllocateInfo command_buffer_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.pNext = nullptr,
		.commandPool = vkstate.command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};

	if (vkAllocateCommandBuffers(vkstate.device, &command_buffer_allocate_info, &vkstate.command_buffer) != VK_SUCCESS) {
		GLVKDEBUG(GLVK_TYPE_VULKAN, GLVK_SEVERITY_ERROR, "Failed to allocate command buffer");
		return 1;
	}

	VkSemaphoreCreateInfo semaphore_create_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
	};

	if (vkCreateSemaphore(vkstate.device, &semaphore_create_info, vkstate.allocator, &vkstate.render_finished) != VK_SUCCESS) {
		GLVKDEBUG(GLVK_TYPE_VULKAN, GLVK_SEVERITY_ERROR, "Failed to create semaphore");
		return 1;
	}

	if (vkCreateSemaphore(vkstate.device, &semaphore_create_info, vkstate.allocator, &vkstate.image_available) != VK_SUCCESS) {
		GLVKDEBUG(GLVK_TYPE_VULKAN, GLVK_SEVERITY_ERROR, "Failed to create semaphore");
		return 1;
	}

	VkFenceCreateInfo fence_create_info = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.pNext = nullptr,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT,
	};

	if (vkCreateFence(vkstate.device, &fence_create_info, vkstate.allocator, &vkstate.in_flight_fence) != VK_SUCCESS) {
		GLVKDEBUG(GLVK_TYPE_VULKAN, GLVK_SEVERITY_ERROR, "Failed to create fence");
		return 1;
	}

	state.inited = true;
	GLVKDEBUG(GLVK_TYPE_GLVK, GLVK_SEVERITY_VERBOSE, "Initialization success");
	return 0;
}

void glvkDraw() {
	if (!state.inited) {
		return;
	}

	vkWaitForFences(vkstate.device, 1, &vkstate.in_flight_fence, VK_TRUE, std::numeric_limits<uint64_t>::max());
	vkResetFences(vkstate.device, 1, &vkstate.in_flight_fence);

	uint32_t image_index = 0;
	vkAcquireNextImageKHR(vkstate.device, vkstate.swapchain, std::numeric_limits<uint64_t>::max(), vkstate.image_available, VK_NULL_HANDLE, &image_index);

	vkResetCommandBuffer(vkstate.command_buffer, 0);

	VkCommandBufferBeginInfo command_buffer_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = nullptr,
	};

	vkBeginCommandBuffer(vkstate.command_buffer, &command_buffer_begin_info);

	VkClearValue clear_value = {
		.color = { 0.0f, 0.0f, 0.0f, 1.0f },
	};

	VkRenderPassBeginInfo render_pass_begin_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.pNext = nullptr,
		.renderPass = vkstate.render_pass,
		.framebuffer = vkstate.framebuffers[image_index],
		.renderArea = {
			.offset = { 0, 0 },
			.extent = vkstate.extent,
		},
		.clearValueCount = 1,
		.pClearValues = &clear_value,
	};

	vkCmdBeginRenderPass(vkstate.command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(vkstate.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkstate.pipeline);

	vkCmdSetViewport(vkstate.command_buffer, 0, 1, &vkstate.viewport);
	vkCmdSetScissor(vkstate.command_buffer, 0, 1, &vkstate.scissor);

	vkCmdDraw(vkstate.command_buffer, 3, 1, 0, 0);
	vkCmdEndRenderPass(vkstate.command_buffer);

	vkEndCommandBuffer(vkstate.command_buffer);

	VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = nullptr,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &vkstate.image_available,
		.pWaitDstStageMask = wait_stages,
		.commandBufferCount = 1,
		.pCommandBuffers = &vkstate.command_buffer,
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &vkstate.render_finished,
	};

	vkQueueSubmit(vkstate.graphics_queue, 1, &submit_info, VK_NULL_HANDLE);

	VkPresentInfoKHR present_info = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.pNext = nullptr,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &vkstate.render_finished,
		.swapchainCount = 1,
		.pSwapchains = &vkstate.swapchain,
		.pImageIndices = &image_index,
		.pResults = nullptr,
	};

	vkQueuePresentKHR(vkstate.present_queue, &present_info);
	vkQueueWaitIdle(vkstate.present_queue);
}

void glvkDeinit() {
	if (!state.inited) {
		return;
	}
	state.inited = false;

	vkDeviceWaitIdle(vkstate.device);
	vkDestroySemaphore(vkstate.device, vkstate.image_available, vkstate.allocator);
	vkDestroySemaphore(vkstate.device, vkstate.render_finished, vkstate.allocator);
	vkDestroyFence(vkstate.device, vkstate.in_flight_fence, vkstate.allocator);
	vkFreeCommandBuffers(vkstate.device, vkstate.command_pool, 1, &vkstate.command_buffer);
	vkDestroyCommandPool(vkstate.device, vkstate.command_pool, vkstate.allocator);
	vkDestroyShaderModule(vkstate.device, vkstate.vshader, vkstate.allocator);
	vkDestroyShaderModule(vkstate.device, vkstate.fshader, vkstate.allocator);
	vkDestroyDescriptorSetLayout(vkstate.device, vkstate.desc_layout, vkstate.allocator);
	vkDestroyPipelineLayout(vkstate.device, vkstate.pipeline_layout, vkstate.allocator);
	vkDestroyPipeline(vkstate.device, vkstate.pipeline, vkstate.allocator);
	for (size_t i = 0; i < vkstate.swapchain_image_count; ++i) {
		vkDestroyFramebuffer(vkstate.device, vkstate.framebuffers[i], vkstate.allocator);
	}
	vkDestroyRenderPass(vkstate.device, vkstate.render_pass, vkstate.allocator);
	for (size_t i = 0; i < vkstate.swapchain_image_count; ++i) {
		vkDestroyImageView(vkstate.device, vkstate.swapchain_views[i], vkstate.allocator);
	}
	vkDestroySwapchainKHR(vkstate.device, vkstate.swapchain, vkstate.allocator);
	vkDestroyDevice(vkstate.device, vkstate.allocator);
	vkDestroySurfaceKHR(vkstate.instance, vkstate.surface, vkstate.allocator);
	if (vkstate.debug_messenger != VK_NULL_HANDLE) {
		PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(vkstate.instance, "vkDestroyDebugUtilsMessengerEXT"));
		if (vkDestroyDebugUtilsMessengerEXT != nullptr) {
			vkDestroyDebugUtilsMessengerEXT(vkstate.instance, vkstate.debug_messenger, vkstate.allocator);
		}
	}
	vkDestroyInstance(vkstate.instance, nullptr);
}

GLenum glGetError(void) {
	if (glstate.errors.empty()) {
		return GL_NO_ERROR;
	}

	GLenum error = glstate.errors.top();
	glstate.errors.pop();

	return error;
}

void glGenBuffers(GLsizei n, GLuint* buffers) {
	if (n < 1) {
		GLPUSHERROR(GL_INVALID_VALUE);
		return;
	}

	for (GLsizei i = 0; i < n; ++i) {
		glbuffer_t buffer = {
			.id = static_cast<GLuint>(glstate.buffers.size()) + 1,
			.buffer = VK_NULL_HANDLE,
			.memory = VK_NULL_HANDLE,
			.size = 0,
		};

		glstate.buffers.push_back(buffer);
		buffers[i] = buffer.id;
	}
}

void glBindBuffer(GLenum target, GLuint buffer) {
	if (
		target != GL_ARRAY_BUFFER &&
		target != GL_ELEMENT_ARRAY_BUFFER &&
		target != GL_COPY_READ_BUFFER &&
		target != GL_COPY_WRITE_BUFFER &&
		target != GL_PIXEL_PACK_BUFFER &&
		target != GL_PIXEL_UNPACK_BUFFER &&
		target != GL_TRANSFORM_FEEDBACK_BUFFER &&
		target != GL_UNIFORM_BUFFER &&
		target != GL_SHADER_STORAGE_BUFFER &&
		target != GL_TEXTURE_BUFFER
	) {
		glPushError(GL_INVALID_ENUM);
		return;
	}

	if (buffer == 0 || buffer > glstate.buffers.size()) {
		glPushError(GL_INVALID_VALUE);
		return;
	}

	if (target == GL_ARRAY_BUFFER) {
		glstate.bound_buffers.array = buffer;
	} else if (target == GL_ELEMENT_ARRAY_BUFFER) {
		glstate.bound_buffers.element_array = buffer;
	} else if (target == GL_COPY_READ_BUFFER) {
		glstate.bound_buffers.copy_read = buffer;
	} else if (target == GL_COPY_WRITE_BUFFER) {
		glstate.bound_buffers.copy_write = buffer;
	} else if (target == GL_PIXEL_PACK_BUFFER) {
		glstate.bound_buffers.pixel_pack = buffer;
	} else if (target == GL_PIXEL_UNPACK_BUFFER) {
		glstate.bound_buffers.pixel_unpack = buffer;
	} else if (target == GL_TRANSFORM_FEEDBACK_BUFFER) {
		glstate.bound_buffers.transform_feedback = buffer;
	} else if (target == GL_UNIFORM_BUFFER) {
		glstate.bound_buffers.uniform = buffer;
	} else if (target == GL_SHADER_STORAGE_BUFFER) {
		glstate.bound_buffers.shader_storage = buffer;
	} else if (target == GL_TEXTURE_BUFFER) {
		glstate.bound_buffers.texture = buffer;
	} else {
		glPushError(GL_INVALID_ENUM);
		return;
	}
}

void glBufferData(GLenum target, GLsizei size, const GLvoid* data, GLenum usage) {
	GLuint buffer = 0;
	if (target == GL_ARRAY_BUFFER) {
		buffer = glstate.bound_buffers.array;
	} else if (target == GL_ELEMENT_ARRAY_BUFFER) {
		buffer = glstate.bound_buffers.element_array;
	} else if (target == GL_COPY_READ_BUFFER) {
		buffer = glstate.bound_buffers.copy_read;
	} else if (target == GL_COPY_WRITE_BUFFER) {
		buffer = glstate.bound_buffers.copy_write;
	} else if (target == GL_PIXEL_PACK_BUFFER) {
		buffer = glstate.bound_buffers.pixel_pack;
	} else if (target == GL_PIXEL_UNPACK_BUFFER) {
		buffer = glstate.bound_buffers.pixel_unpack;
	} else if (target == GL_TRANSFORM_FEEDBACK_BUFFER) {
		buffer = glstate.bound_buffers.transform_feedback;
	} else if (target == GL_UNIFORM_BUFFER) {
		buffer = glstate.bound_buffers.uniform;
	} else if (target == GL_SHADER_STORAGE_BUFFER) {
		buffer = glstate.bound_buffers.shader_storage;
	} else if (target == GL_TEXTURE_BUFFER) {
		buffer = glstate.bound_buffers.texture;
	} else {
		glPushError(GL_INVALID_ENUM);
		return;
	}

	if (buffer == 0 || buffer > glstate.buffers.size()) {
		glPushError(GL_INVALID_OPERATION);
		return;
	}

	if (size < 0) {
		glPushError(GL_INVALID_VALUE);
		return;
	}

	if (usage != GL_STREAM_DRAW || usage != GL_STREAM_READ || usage != GL_STREAM_COPY || usage != GL_STATIC_DRAW || usage != GL_STATIC_READ || usage != GL_STATIC_COPY || usage != GL_DYNAMIC_DRAW || usage != GL_DYNAMIC_READ || usage != GL_DYNAMIC_COPY) {
		glPushError(GL_INVALID_ENUM);
		return;
	}

	glbuffer_t& glbuffer = glstate.buffers[buffer - 1];
	if (glbuffer.buffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(vkstate.device, glbuffer.buffer, vkstate.allocator);
		vkFreeMemory(vkstate.device, glbuffer.memory, vkstate.allocator);
	}

	VkBufferCreateInfo buffer_create_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.size = static_cast<VkDeviceSize>(size),
		.usage = 0,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices = nullptr,
	};


}

void glDeleteBuffers(GLsizei n, const GLuint *buffers) {
	if (n < 1 || buffers == nullptr) {
		glPushError(GL_INVALID_VALUE);
		return;
	}

	for (GLsizei i = 0; i < n; ++i) {
		if (buffers[i] == 0 || buffers[i] > glstate.buffers.size()) {
			glPushError(GL_INVALID_VALUE);
			return;
		}

		glbuffer_t& glbuffer = glstate.buffers[buffers[i] - 1];
		if (glbuffer.buffer != VK_NULL_HANDLE) {
			vkDestroyBuffer(vkstate.device, glbuffer.buffer, vkstate.allocator);
			vkFreeMemory(vkstate.device, glbuffer.memory, vkstate.allocator);
		}
	}
}
