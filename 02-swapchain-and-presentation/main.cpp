// vulkan.h has to come before SDL_vulkan.h, so SDL uses the real Vulkan types.
#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <format>
#include <limits>
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

VkSurfaceFormatKHR chooseSurfaceFormat(VkPhysicalDevice gpu, VkSurfaceKHR surface)
{
    const auto formats = enumerate<VkSurfaceFormatKHR>(vkGetPhysicalDeviceSurfaceFormatsKHR, gpu, surface);
    const auto best = std::ranges::find_if(formats, [](const VkSurfaceFormatKHR& f) {
        return f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    return best != formats.end() ? *best : formats.front();
}

// FIFO works everywhere. Try VK_PRESENT_MODE_MAILBOX_KHR or VK_PRESENT_MODE_IMMEDIATE_KHR here too.
constexpr VkPresentModeKHR preferredPresentMode = VK_PRESENT_MODE_FIFO_KHR;

VkPresentModeKHR choosePresentMode(VkPhysicalDevice gpu, VkSurfaceKHR surface)
{
    const auto modes = enumerate<VkPresentModeKHR>(vkGetPhysicalDeviceSurfacePresentModesKHR, gpu, surface);
    return std::ranges::contains(modes, preferredPresentMode) ? preferredPresentMode : VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& caps, SDL_Window* window)
{
    if (caps.currentExtent.width != std::numeric_limits<std::uint32_t>::max())
        return caps.currentExtent;

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    return {
        .width  = std::clamp(static_cast<std::uint32_t>(width), caps.minImageExtent.width, caps.maxImageExtent.width),
        .height = std::clamp(static_cast<std::uint32_t>(height), caps.minImageExtent.height, caps.maxImageExtent.height),
    };
}

struct Swapchain {
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    std::vector<VkImage> images;
};

Swapchain createSwapchain(VkPhysicalDevice gpu, VkDevice device, VkSurfaceKHR surface, SDL_Window* window,
                          const QueueFamilies& families, VkSwapchainKHR old)
{
    VkSurfaceCapabilitiesKHR caps;
    vkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps));

    const VkSurfaceFormatKHR format = chooseSurfaceFormat(gpu, surface);
    const VkPresentModeKHR presentMode = choosePresentMode(gpu, surface);
    const VkExtent2D extent = chooseExtent(caps, window);

    std::uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount != 0)
        imageCount = std::min(imageCount, caps.maxImageCount);

    const std::array familyIndices = { *families.graphics, *families.present };
    const bool shared = families.graphics != families.present;

    const VkSwapchainCreateInfoKHR info{
        .sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface               = surface,
        .minImageCount         = imageCount,
        .imageFormat           = format.format,
        .imageColorSpace       = format.colorSpace,
        .imageExtent           = extent,
        .imageArrayLayers      = 1,
        .imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode      = shared ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = shared ? 2u : 0u,
        .pQueueFamilyIndices   = shared ? familyIndices.data() : nullptr,
        .preTransform          = caps.currentTransform,
        .compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode           = presentMode,
        .clipped               = VK_TRUE,
        .oldSwapchain          = old,
    };

    Swapchain swapchain{ .format = format.format, .extent = extent, .presentMode = presentMode };
    vkCheck(vkCreateSwapchainKHR(device, &info, nullptr, &swapchain.handle));
    swapchain.images = enumerate<VkImage>(vkGetSwapchainImagesKHR, device, swapchain.handle);
    return swapchain;
}

struct Transition {
    VkImageLayout from = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout to = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 srcStage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 srcAccess = VK_ACCESS_2_NONE;
    VkPipelineStageFlags2 dstStage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 dstAccess = VK_ACCESS_2_NONE;
};

void transition(VkCommandBuffer cmd, VkImage image, const Transition& t)
{
    const VkImageMemoryBarrier2 barrier{
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask        = t.srcStage,
        .srcAccessMask       = t.srcAccess,
        .dstStageMask        = t.dstStage,
        .dstAccessMask       = t.dstAccess,
        .oldLayout           = t.from,
        .newLayout           = t.to,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image,
        .subresourceRange    = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 },
    };
    const VkDependencyInfo dependency{
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &barrier,
    };
    vkCmdPipelineBarrier2(cmd, &dependency);
}

int main()
{
    sdlCheck(SDL_Init(SDL_INIT_VIDEO));

    SDL_Window* window = SDL_CreateWindow("Vulkan from Scratch", 1280, 720, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
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

    const VkPhysicalDeviceVulkan13Features features13{
        .sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE,
    };

    const VkDeviceCreateInfo deviceInfo{
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = &features13,
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

    Swapchain swapchain = createSwapchain(gpu, device, surface, window, families, VK_NULL_HANDLE);
    std::println("Swapchain: {}x{}, {} images, {}", swapchain.extent.width, swapchain.extent.height,
                 swapchain.images.size(), enumName(swapchain.presentMode));

    const VkCommandPoolCreateInfo poolInfo{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = *families.graphics,
    };
    VkCommandPool commandPool = VK_NULL_HANDLE;
    vkCheck(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool));

    const VkCommandBufferAllocateInfo allocInfo{
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = commandPool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkCheck(vkAllocateCommandBuffers(device, &allocInfo, &cmd));

    const VkSemaphoreCreateInfo semaphoreInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    const VkFenceCreateInfo fenceInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkFence frameDone = VK_NULL_HANDLE;
    vkCheck(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailable));
    vkCheck(vkCreateFence(device, &fenceInfo, nullptr, &frameDone));

    auto createSemaphores = [&](std::size_t count) {
        std::vector<VkSemaphore> semaphores(count);
        for (VkSemaphore& semaphore : semaphores)
            vkCheck(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore));
        return semaphores;
    };
    std::vector<VkSemaphore> renderFinished = createSemaphores(swapchain.images.size());

    auto recreateSwapchain = [&] {
        vkCheck(vkDeviceWaitIdle(device));
        Swapchain fresh = createSwapchain(gpu, device, surface, window, families, swapchain.handle);
        vkDestroySwapchainKHR(device, swapchain.handle, nullptr);
        for (VkSemaphore semaphore : renderFinished)
            vkDestroySemaphore(device, semaphore, nullptr);
        swapchain = std::move(fresh);
        renderFinished = createSemaphores(swapchain.images.size());
    };

    bool running = true;
    bool resized = false;
    int frames = 0;
    auto fpsTimer = std::chrono::steady_clock::now();

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT)
                running = false;
            if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
                resized = true;
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_WaitEvent(nullptr);
            continue;
        }

        vkCheck(vkWaitForFences(device, 1, &frameDone, VK_TRUE, std::numeric_limits<std::uint64_t>::max()));

        std::uint32_t imageIndex = 0;
        const VkResult acquired = vkAcquireNextImageKHR(device, swapchain.handle, std::numeric_limits<std::uint64_t>::max(),
                                                        imageAvailable, VK_NULL_HANDLE, &imageIndex);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapchain();
            continue;
        }
        if (acquired != VK_SUBOPTIMAL_KHR)
            vkCheck(acquired);

        vkCheck(vkResetFences(device, 1, &frameDone));
        vkCheck(vkResetCommandBuffer(cmd, 0));

        const VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        vkCheck(vkBeginCommandBuffer(cmd, &beginInfo));

        const VkImage image = swapchain.images[imageIndex];
        transition(cmd, image, {
            .from      = VK_IMAGE_LAYOUT_UNDEFINED,
            .to        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcStage  = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstStage  = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        });

        const float t = SDL_GetTicks() / 1000.0f;
        const VkClearColorValue color{ .float32 = {
            0.5f + 0.5f * std::sin(t),
            0.5f + 0.5f * std::sin(t + 2.1f),
            0.5f + 0.5f * std::sin(t + 4.2f),
            1.0f,
        } };
        const VkImageSubresourceRange whole{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 };
        vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &whole);

        transition(cmd, image, {
            .from      = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .to        = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcStage  = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        });

        vkCheck(vkEndCommandBuffer(cmd));

        const VkSemaphoreSubmitInfo waitInfo{
            .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = imageAvailable,
            .stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        };
        const VkSemaphoreSubmitInfo signalInfo{
            .sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = renderFinished[imageIndex],
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        };
        const VkCommandBufferSubmitInfo cmdInfo{
            .sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = cmd,
        };
        const VkSubmitInfo2 submit{
            .sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .waitSemaphoreInfoCount   = 1,
            .pWaitSemaphoreInfos      = &waitInfo,
            .commandBufferInfoCount   = 1,
            .pCommandBufferInfos      = &cmdInfo,
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos    = &signalInfo,
        };
        vkCheck(vkQueueSubmit2(graphicsQueue, 1, &submit, frameDone));

        const VkPresentInfoKHR presentInfo{
            .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores    = &renderFinished[imageIndex],
            .swapchainCount     = 1,
            .pSwapchains        = &swapchain.handle,
            .pImageIndices      = &imageIndex,
        };
        const VkResult presented = vkQueuePresentKHR(presentQueue, &presentInfo);
        if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR || resized) {
            resized = false;
            recreateSwapchain();
        } else {
            vkCheck(presented);
        }

        ++frames;
        const auto now = std::chrono::steady_clock::now();
        if (now - fpsTimer >= std::chrono::seconds(1)) {
            SDL_SetWindowTitle(window, std::format("Vulkan from Scratch | {} fps", frames).c_str());
            frames = 0;
            fpsTimer = now;
        }
    }

    vkCheck(vkDeviceWaitIdle(device));

    for (VkSemaphore semaphore : renderFinished)
        vkDestroySemaphore(device, semaphore, nullptr);
    vkDestroySemaphore(device, imageAvailable, nullptr);
    vkDestroyFence(device, frameDone, nullptr);
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroySwapchainKHR(device, swapchain.handle, nullptr);

    vkDestroyDevice(device, nullptr);
    SDL_Vulkan_DestroySurface(instance, surface, nullptr);

    const auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    destroyMessenger(instance, messenger, nullptr);

    vkDestroyInstance(instance, nullptr);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
