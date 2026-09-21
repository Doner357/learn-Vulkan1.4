#ifndef GPLAB_GRAPHIC_LAB_HPP
#define GPLAB_GRAPHIC_LAB_HPP

#include <vulkan/vulkan_raii.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace gplab
{

constexpr std::uint32_t kWindowWidth  = 800;
constexpr std::uint32_t kWindowHeight = 600;

constexpr auto const kRequiredValidationLayers   = std::to_array({"VK_LAYER_KHRONOS_validation"});
constexpr auto const kRequiredInstanceExtensions = std::to_array({vk::EXTDebugUtilsExtensionName});
constexpr auto const kRequiredDeviceExtensions   = std::to_array({vk::KHRSwapchainExtensionName});

#ifdef NDEBUG
constexpr bool kEnableValidationLayers = false;
#else
constexpr bool kEnableValidationLayers = true;
#endif // NDEBUG

/*
 * Main Vulkan Lab class define as singleton
 */
class GraphicLab
{
    public:
        GraphicLab(GraphicLab& other)           = delete;
        GraphicLab(GraphicLab&& other)          = delete;
        void operator=(GraphicLab const& ohter) = delete;
        void operator=(GraphicLab&& other)      = delete;
        ~GraphicLab()                           = default;

        static GraphicLab& getInstance();

        void run();

    private:
        GLFWwindow* window = nullptr;

        static constexpr float kQueuePriority = 0.5f;

        // Context
        vk::raii::Context context;
        // Instance
        vk::raii::Instance instance                      = nullptr;
        vk::raii::DebugUtilsMessengerEXT debug_messenger = nullptr;
        // Physical Device
        vk::raii::SurfaceKHR surface             = nullptr;
        vk::raii::PhysicalDevice physical_device = nullptr;
        // Logical Device
        vk::raii::Device device = nullptr;
        // Queue Family
        std::uint32_t queue_index{};
        vk::raii::Queue queue = nullptr;
        // Swapchain
        vk::raii::SwapchainKHR swapchain = nullptr;
        vk::Extent2D swapchain_extent{};
        vk::SurfaceFormatKHR swapchain_surface_format{};
        std::vector<vk::Image> swapchain_images;
        std::vector<vk::raii::ImageView> swapchain_image_views;
        // Pipeline
        vk::raii::PipelineLayout pipeline_layout = nullptr;
        vk::raii::Pipeline graphics_pipeline     = nullptr;
        // Command Buffer
        vk::raii::CommandPool command_pool     = nullptr;
        vk::raii::CommandBuffer command_buffer = nullptr;
        // Synchronization
        vk::raii::Semaphore present_complete_semaphore = nullptr;
        vk::raii::Semaphore render_finished_semaphore  = nullptr;
        vk::raii::Fence draw_fence                     = nullptr;

        GraphicLab() = default;

        void initWindow();
        void initVulkan();
        void createInstance();
        void createSurface();
        void setupDebugMessenger();
        void pickPhysicalDevice();
        void createLogicalDevice();
        void createSwapchain();
        void createImageViews();
        void createGraphicsPipeline();
        void createCommandPool();
        void createCommandBuffer();
        void createSyncObjects();
        void reacordCommandBuffer(std::uint32_t image_index);
        void mainLoop();
        void drawFrame();
        void cleanup();
};

} // namespace gplab

#endif // GPLAB_GRAPHIC_LAB_HPP