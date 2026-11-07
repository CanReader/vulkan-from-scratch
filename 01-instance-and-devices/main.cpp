// vulkan.h has to come before SDL_vulkan.h, so SDL uses the real Vulkan types.
#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <optional>
#include <print>
#include <ranges>
#include <set>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef __cpp_impl_reflection
#include <meta>
#endif

// The name of an enum value, like "VK_ERROR_LAYER_NOT_PRESENT".
template <typename E>
    requires std::is_enum_v<E>
std::string enumName(E value)
{
#ifdef __cpp_impl_reflection
    template for (constexpr auto e : std::define_static_array(std::meta::enumerators_of(^^E))) {
        if (value == [:e:])
            return std::string{std::meta::identifier_of(e)};
    }
#endif
    return std::to_string(std::to_underlying(value));
}

void vkCheck(VkResult result, std::source_location where = std::source_location::current())
{
    if (result == VK_SUCCESS)
        return;
    std::println(stderr, "{}:{}: Vulkan call failed with {}", where.file_name(), where.line(), enumName(result));
    std::abort();
}

void sdlCheck(bool ok, std::source_location where = std::source_location::current())
{
    if (ok)
        return;
    std::println(stderr, "{}:{}: {}", where.file_name(), where.line(), SDL_GetError());
    std::abort();
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*)
{
    std::println(stderr, "[vulkan] {}", data->pMessage);
    return VK_FALSE;
}

template <typename T, typename Fn, typename... Args>
std::vector<T> enumerate(Fn fn, Args... args)
{
    std::uint32_t count = 0;
    fn(args..., &count, nullptr);
    std::vector<T> items(count);
    fn(args..., &count, items.data());
    return items;
}

struct QueueFamilies {
    std::optional<std::uint32_t> graphics;
    std::optional<std::uint32_t> present;

    bool complete() const { return graphics && present; }
};

QueueFamilies findQueueFamilies(VkPhysicalDevice gpu, VkSurfaceKHR surface)
{
    const auto families = enumerate<VkQueueFamilyProperties>(vkGetPhysicalDeviceQueueFamilyProperties, gpu);

    QueueFamilies result;
    for (const auto [index, family] : std::views::enumerate(families)) {
        const auto i = static_cast<std::uint32_t>(index);
        const bool graphics = family.queueFlags & VK_QUEUE_GRAPHICS_BIT;
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &present);

        // One family that does both is the best case.
        if (graphics && present)
            return { i, i };
        if (graphics && !result.graphics) result.graphics = i;
        if (present && !result.present) result.present = i;
    }
    return result;
}

bool supportsSwapchain(VkPhysicalDevice gpu)
{
    const auto extensions = enumerate<VkExtensionProperties>(vkEnumerateDeviceExtensionProperties, gpu, nullptr);
    return std::ranges::contains(extensions, std::string_view{VK_KHR_SWAPCHAIN_EXTENSION_NAME},
                                 [](const VkExtensionProperties& ext) { return std::string_view{ext.extensionName}; });
}

std::expected<VkPhysicalDevice, std::string> pickGpu(VkInstance instance, VkSurfaceKHR surface)
{
    std::optional<VkPhysicalDevice> best;
    for (VkPhysicalDevice gpu : enumerate<VkPhysicalDevice>(vkEnumeratePhysicalDevices, instance)) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(gpu, &props);

        if (props.apiVersion < VK_API_VERSION_1_3) continue;
        if (!findQueueFamilies(gpu, surface).complete()) continue;
        if (!supportsSwapchain(gpu)) continue;

        best = gpu;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) break;
    }

    if (!best)
        return std::unexpected("No GPU with Vulkan 1.3, a graphics queue and a swapchain");
    return *best;
}

int main()
{
    sdlCheck(SDL_Init(SDL_INIT_VIDEO));

    SDL_Window* window = SDL_CreateWindow("Vulkan from Scratch", 1280, 720, SDL_WINDOW_VULKAN);
    sdlCheck(window != nullptr);

    const VkApplicationInfo appInfo{
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName   = "Vulkan from Scratch",
        .applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
        .pEngineName        = "No Engine",
        .engineVersion      = VK_MAKE_API_VERSION(0, 1, 0, 0),
        .apiVersion         = VK_API_VERSION_1_3,
    };

    Uint32 sdlCount = 0;
    const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlCount);
    sdlCheck(sdlExtensions != nullptr);

    const auto extensions = std::views::concat(std::span(sdlExtensions, sdlCount),
                                               std::array{VK_EXT_DEBUG_UTILS_EXTENSION_NAME})
                          | std::ranges::to<std::vector>();

    constexpr std::array layers = { "VK_LAYER_KHRONOS_validation" };

    const VkInstanceCreateInfo instanceInfo{
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo        = &appInfo,
        .enabledLayerCount       = static_cast<std::uint32_t>(layers.size()),
        .ppEnabledLayerNames     = layers.data(),
        .enabledExtensionCount   = static_cast<std::uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    VkInstance instance = VK_NULL_HANDLE;
    vkCheck(vkCreateInstance(&instanceInfo, nullptr, &instance));

    const VkDebugUtilsMessengerCreateInfoEXT debugInfo{
        .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback,
    };

    const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));

    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    vkCheck(createMessenger(instance, &debugInfo, nullptr, &messenger));

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    sdlCheck(SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface));

    const auto picked = pickGpu(instance, surface);
    if (!picked) {
        std::println(stderr, "{}", picked.error());
        return EXIT_FAILURE;
    }
    VkPhysicalDevice gpu = *picked;

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(gpu, &props);
    std::println("Using {} ({})", std::string_view{props.deviceName}, enumName(props.deviceType));

    const QueueFamilies families = findQueueFamilies(gpu, surface);
    const std::set uniqueFamilies = { *families.graphics, *families.present };

    const float priority = 1.0f;
    const auto queueInfos = uniqueFamilies
        | std::views::transform([&](std::uint32_t family) {
              return VkDeviceQueueCreateInfo{
                  .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                  .queueFamilyIndex = family,
                  .queueCount       = 1,
                  .pQueuePriorities = &priority,
              };
          })
        | std::ranges::to<std::vector>();

    constexpr std::array deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    const VkDeviceCreateInfo deviceInfo{
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount    = static_cast<std::uint32_t>(queueInfos.size()),
        .pQueueCreateInfos       = queueInfos.data(),
        .enabledExtensionCount   = static_cast<std::uint32_t>(deviceExtensions.size()),
        .ppEnabledExtensionNames = deviceExtensions.data(),
    };

    VkDevice device = VK_NULL_HANDLE;
    vkCheck(vkCreateDevice(gpu, &deviceInfo, nullptr, &device));

    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, *families.graphics, 0, &graphicsQueue);
    vkGetDeviceQueue(device, *families.present, 0, &presentQueue);

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT)
                running = false;
        }
    }

    vkDestroyDevice(device, nullptr);
    SDL_Vulkan_DestroySurface(instance, surface, nullptr);

    const auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    destroyMessenger(instance, messenger, nullptr);

    vkDestroyInstance(instance, nullptr);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
