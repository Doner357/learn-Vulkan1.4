#include "graphic-lab.hpp"

#include <vulkan/vulkan_raii.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <print>
#include <string_view>
#include <vector>

namespace
{

std::vector<char const*> getRequiredInstanceExtensions()
{
    // Get the required instance extensions from GLFW.
    std::uint32_t glfw_extension_count = 0;
    auto* const glfw_extensions        = glfwGetRequiredInstanceExtensions(&glfw_extension_count);

    std::vector<char const*> extensions(glfw_extensions, glfw_extensions + glfw_extension_count);

    if (gplab::kEnableValidationLayers)
    {
        extensions.append_range(gplab::kRequiredInstanceExtensions);
    }

    return extensions;
}

VKAPI_ATTR vk::Bool32 VKAPI_CALL
debugCallback([[maybe_unused]] vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
              vk::DebugUtilsMessageTypeFlagsEXT type,
              [[maybe_unused]] vk::DebugUtilsMessengerCallbackDataEXT const* pCallbackData,
              [[maybe_unused]] void* pUserData)
{
    std::cerr << "validation layer: type " << vk::to_string(type)
              << " msg: " << pCallbackData->pMessage << '\n';
    return vk::False;
}

bool isDeviceSuitable(vk::raii::PhysicalDevice const& physical_device,
                      vk::raii::SurfaceKHR const& surface)
{
    // API version check
    bool supports_vulkan1_4 = physical_device.getProperties().apiVersion >= vk::ApiVersion14;

    // Queue family check
    auto queue_families          = physical_device.getQueueFamilyProperties();
    bool supports_required_queue = false;
    for (std::uint32_t i = 0; i < queue_families.size(); ++i)
    {
        if ((queue_families[i].queueFlags & vk::QueueFlagBits::eGraphics) &&
            (physical_device.getSurfaceSupportKHR(i, *surface) == vk::True))
        {
            supports_required_queue = true;
            break;
        }
    }

    // Required extension check
    auto available_device_extensions      = physical_device.enumerateDeviceExtensionProperties();
    bool supports_all_required_extensions = std::ranges::all_of(
        gplab::kRequiredDeviceExtensions,
        [&available_device_extensions](auto const& required_extension)
        {
            return std::ranges::any_of(available_device_extensions,
                                       [required_extension](auto const& ext)
                                       {
                                           return std::strcmp(ext.extensionName,
                                                              required_extension) == 0;
                                       });
        });

    // Required features check
    auto features =
        physical_device.getFeatures2<vk::PhysicalDeviceFeatures2,
                                     vk::PhysicalDeviceVulkan11Features,
                                     vk::PhysicalDeviceVulkan13Features,
                                     vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();

    bool supports_required_features =
        static_cast<bool>(features.get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy) &&
        static_cast<bool>(
            features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters) &&
        static_cast<bool>(features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering) &&
        static_cast<bool>(features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2) &&
        static_cast<bool>(
            features.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState);

    return supports_vulkan1_4 && supports_required_queue && supports_all_required_extensions &&
           supports_required_features;
}

vk::SurfaceFormatKHR chooseSwapSurfaceFormat(
    std::vector<vk::SurfaceFormatKHR> const& available_formats)
{
    assert(!available_formats.empty());

    auto const format_it =
        std::ranges::find_if(available_formats,
                             [](auto const& format)
                             {
                                 return format.format == vk::Format::eB8G8R8A8Srgb;
                             });
    return format_it != available_formats.end() ? *format_it : available_formats[0];
}

vk::PresentModeKHR chooseSwapPresentMode(
    std::vector<vk::PresentModeKHR> const& available_present_modes)
{
    assert(std::ranges::any_of(available_present_modes,
                               [](auto present_mode)
                               {
                                   return present_mode == vk::PresentModeKHR::eFifo;
                               }));
    return std::ranges::any_of(available_present_modes,
                               [](vk::PresentModeKHR const value)
                               {
                                   return vk::PresentModeKHR::eMailbox == value;
                               })
               ? vk::PresentModeKHR::eMailbox
               : vk::PresentModeKHR::eFifo;
}

vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR const& surface_capabilities,
                              GLFWwindow* glfw_window)
{
    if (surface_capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max())
    {
        return surface_capabilities.currentExtent;
    }

    int width{};
    int height{};
    glfwGetFramebufferSize(glfw_window, &width, &height);

    return {
        .width  = std::clamp<std::uint32_t>(width,
                                            surface_capabilities.minImageExtent.width,
                                            surface_capabilities.maxImageExtent.width),
        .height = std::clamp<std::uint32_t>(height,
                                            surface_capabilities.minImageExtent.height,
                                            surface_capabilities.maxImageExtent.height),
    };
}

std::uint32_t chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const& surface_capabilities)
{
    auto min_image_count = std::max(3u, surface_capabilities.minImageCount);
    if ((0 < surface_capabilities.maxImageCount) &&
        (surface_capabilities.maxImageCount < min_image_count))
    {
        min_image_count = surface_capabilities.maxImageCount;
    }

    return min_image_count;
}

std::vector<char> readFile(std::string const& filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
        throw std::runtime_error("failed to open file!");
    }

    std::vector<char> buffer(file.tellg());

    file.seekg(0, std::ios::beg);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));

    file.close();

    return buffer;
}

[[nodiscard]] vk::raii::ShaderModule createShaderModule(vk::raii::Device const& device,
                                                        std::vector<char> const& code)
{
    vk::ShaderModuleCreateInfo create_info{
        .codeSize = code.size() * sizeof(char),
        .pCode    = reinterpret_cast<uint32_t const*>(code.data()),
    };

    vk::raii::ShaderModule shader_module = device.createShaderModule(create_info);

    return shader_module;
}

void transitionImageLayout(vk::Image const& image, vk::ImageLayout old_layout,
                           vk::ImageLayout new_layout, vk::AccessFlagBits2 src_access_mask,
                           vk::AccessFlagBits2 dst_access_mask,
                           vk::PipelineStageFlagBits2 src_stage_mask,
                           vk::PipelineStageFlagBits2 dst_stage_mask,
                           vk::raii::CommandBuffer const& command_buffer)
{
    vk::ImageMemoryBarrier2 barrier = {
        .srcStageMask        = src_stage_mask,
        .srcAccessMask       = src_access_mask,
        .dstStageMask        = dst_stage_mask,
        .dstAccessMask       = dst_access_mask,
        .oldLayout           = old_layout,
        .newLayout           = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image,
        .subresourceRange    = {
            .aspectMask     = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };

    vk::DependencyInfo dependency_info =
        {.dependencyFlags = {}, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};

    command_buffer.pipelineBarrier2(dependency_info);
}

} // namespace

namespace gplab
{

GraphicLab& GraphicLab::getInstance()
{
    static GraphicLab gplab_instance{};

    return gplab_instance;
}

void GraphicLab::cleanup()
{
    glfwDestroyWindow(window);

    glfwTerminate();
}

void GraphicLab::initWindow()
{
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    window = glfwCreateWindow(kWindowWidth, kWindowHeight, "Vulkan-lab", nullptr, nullptr);
}

void GraphicLab::initVulkan()
{
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapchain();
    createImageViews();
    createGraphicsPipeline();
    createCommandPool();
    createCommandBuffer();
    createSyncObjects();
}

void GraphicLab::createInstance()
{
    constexpr vk::ApplicationInfo app_info{
        .pApplicationName   = "Vulkan-lab",
        .applicationVersion = vk::makeVersion(1, 0, 0),
        .pEngineName        = "No Engine",
        .engineVersion      = vk::makeVersion(1, 0, 0),
        .apiVersion         = vk::ApiVersion14,
    };

    // Get the required layers
    std::vector<char const*> required_layers;
    if (kEnableValidationLayers)
    {
        required_layers.assign(kRequiredValidationLayers.begin(), kRequiredValidationLayers.end());
    }

    // Check if the required layers are supported by the Vulkan implementation.
    auto layer_properties = context.enumerateInstanceLayerProperties();
    std::println("Available layers:");
    for (auto const& layer : layer_properties)
    {
        std::println("\t{}", std::string_view(layer.layerName.data()));
    }
    std::println();

    auto unsupported_layers =
        std::ranges::find_if(required_layers,
                             [&layer_properties](auto const& required_layer)
                             {
                                 return std::ranges::none_of(
                                     layer_properties,
                                     [required_layer](auto const& layer)
                                     {
                                         return std::strcmp(layer.layerName, required_layer) == 0;
                                     });
                             });

    if (unsupported_layers != required_layers.end())
    {
        throw std::runtime_error("Required layer not supported:" +
                                 std::string(*unsupported_layers));
    }

    // Get all the extensions available in the Vulkan implementation.
    auto extension_properties = context.enumerateInstanceExtensionProperties();
    std::println("Available extensions:");
    for (auto const& ext : extension_properties)
    {
        std::println("\t{}", std::string_view(ext.extensionName.data()));
    }
    std::println();

    // Get the required extensions.
    auto required_extensions = getRequiredInstanceExtensions();

    // Check if the required extensions are supported by the Vulkan implementation.
    auto unsupported_extensions = std::ranges::find_if(
        required_extensions,
        [&extension_properties](auto const& required_extension)
        {
            return std::ranges::none_of(extension_properties,
                                        [required_extension](auto const& ext)
                                        {
                                            return std::strcmp(ext.extensionName,
                                                               required_extension) == 0;
                                        });
        });

    if (unsupported_extensions != required_extensions.end())
    {
        throw std::runtime_error("Required extension not supported:" +
                                 std::string(*unsupported_extensions));
    }

    vk::InstanceCreateInfo create_info{
        .pApplicationInfo        = &app_info,
        .enabledLayerCount       = static_cast<std::uint32_t>(required_layers.size()),
        .ppEnabledLayerNames     = required_layers.data(),
        .enabledExtensionCount   = static_cast<std::uint32_t>(required_extensions.size()),
        .ppEnabledExtensionNames = required_extensions.data(),
    };

    instance = context.createInstance(create_info);
}

void GraphicLab::setupDebugMessenger()
{
    if (!kEnableValidationLayers)
    {
        return;
    }

    vk::DebugUtilsMessageSeverityFlagsEXT serverity_flags(
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
    vk::DebugUtilsMessageTypeFlagsEXT message_type_flags(
        vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
        vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance);
    vk::DebugUtilsMessengerCreateInfoEXT debug_utils_messenger_create_info_ext{
        .messageSeverity = serverity_flags,
        .messageType     = message_type_flags,
        .pfnUserCallback = &debugCallback,
    };

    debug_messenger = instance.createDebugUtilsMessengerEXT(debug_utils_messenger_create_info_ext);
}

void GraphicLab::createSurface()
{
    VkSurfaceKHR surface_handle = nullptr;
    if (glfwCreateWindowSurface(*instance, window, nullptr, &surface_handle) !=
        VkResult::VK_SUCCESS)
    {
        throw std::runtime_error("failed to create window surface!");
    }

    surface = vk::raii::SurfaceKHR(instance, surface_handle);
}

void GraphicLab::pickPhysicalDevice()
{
    // Get available physical devices from the Vulkan implementation.
    auto physical_devices = instance.enumeratePhysicalDevices();

    if (physical_devices.empty())
    {
        throw std::runtime_error("Failed to find GPUs with Vulkan support!");
    }

    std::println("Found {} physical devices", physical_devices.size());
    for (auto const& dev : physical_devices)
    {
        std::println("\t{}", std::string_view(dev.getProperties().deviceName.data()));
    }
    auto const dev_iter = std::ranges::find_if(physical_devices,
                                               [&](auto const& phy_dev)
                                               {
                                                   return isDeviceSuitable(phy_dev, surface);
                                               });

    if (dev_iter == physical_devices.end())
    {
        throw std::runtime_error("Failed to find a suitable GPU!");
    }

    physical_device = *dev_iter;
}

void GraphicLab::createLogicalDevice()
{
    // Find a queue family that supports graphics operations.
    auto queue_family_properties = physical_device.getQueueFamilyProperties();
    queue_index                  = ~0u;
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(queue_family_properties.size()); ++i)
    {
        if ((queue_family_properties[i].queueFlags & vk::QueueFlagBits::eGraphics) &&
            (physical_device.getSurfaceSupportKHR(i, *surface) == vk::True))
        {
            queue_index = i;
            break;
        }
    }

    if (queue_index == ~0u)
    {
        throw std::runtime_error(
            "Failed to find a queue family that supports graphics and present -> terminating");
    }

    vk::DeviceQueueCreateInfo device_queue_create_info{
        .queueFamilyIndex = queue_index,
        .queueCount       = 1,
        .pQueuePriorities = &kQueuePriority,
    };

    // Specify the required features for the logical device using a structure chain.
    vk::StructureChain<vk::PhysicalDeviceFeatures2,
                       vk::PhysicalDeviceVulkan11Features,
                       vk::PhysicalDeviceVulkan13Features,
                       vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
        features_chain = {
            {},
            {
                .shaderDrawParameters = vk::True, // PhysicalDeviceFeatures2 (empty for now)
            },
            {
                .synchronization2 = vk::True, // Enbale using ImageBarrier2
                .dynamicRendering = vk::True, // PhysicalDeviceVulkan11Features: Enable shader draw
                                              // parameters from Vulkan 1.1
            },
            {
                .extendedDynamicState =
                    vk::True, // PhysicalDeviceExtendedDynamicStateFeaturesEXT: Enable extended
                              // dynamic state features from Vulkan 1.3
            }
        };

    vk::DeviceCreateInfo device_create_info{
        .pNext                   = &features_chain.get<vk::PhysicalDeviceFeatures2>(),
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &device_queue_create_info,
        .enabledExtensionCount   = static_cast<std::uint32_t>(kRequiredDeviceExtensions.size()),
        .ppEnabledExtensionNames = kRequiredDeviceExtensions.data(),
    };

    device = physical_device.createDevice(device_create_info);
    queue  = device.getQueue(queue_index, 0);
}

void GraphicLab::createSwapchain()
{
    auto surface_capabilities     = physical_device.getSurfaceCapabilitiesKHR(*surface);
    swapchain_extent              = chooseSwapExtent(surface_capabilities, window);
    std::uint32_t min_image_count = chooseSwapMinImageCount(surface_capabilities);

    auto available_formats   = physical_device.getSurfaceFormatsKHR(*surface);
    swapchain_surface_format = chooseSwapSurfaceFormat(available_formats);

    auto available_present_mode = physical_device.getSurfacePresentModesKHR(*surface);

    vk::SwapchainCreateInfoKHR swapchain_create_info{
        .surface          = *surface,
        .minImageCount    = min_image_count,
        .imageFormat      = swapchain_surface_format.format,
        .imageColorSpace  = swapchain_surface_format.colorSpace,
        .imageExtent      = swapchain_extent,
        .imageArrayLayers = 1,
        .imageUsage       = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform     = surface_capabilities.currentTransform,
        .compositeAlpha   = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode      = chooseSwapPresentMode(available_present_mode),
        .clipped          = vk::True,
    };

    swapchain        = vk::raii::SwapchainKHR(device, swapchain_create_info);
    swapchain_images = swapchain.getImages();
}

void GraphicLab::createImageViews()
{
    assert(swapchain_image_views.empty());

    vk::ImageViewCreateInfo image_view_create_info{
        .viewType         = vk::ImageViewType::e2D,
        .format           = swapchain_surface_format.format,
        .subresourceRange = {
            .aspectMask     = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };

    for (auto const& image : swapchain_images)
    {
        image_view_create_info.image = image;
        swapchain_image_views.emplace_back(device, image_view_create_info);
    }
}

void GraphicLab::createGraphicsPipeline()
{
    vk::raii::ShaderModule shader_module =
        createShaderModule(device, readFile("gplab/shaders/slang.spv"));

    vk::PipelineShaderStageCreateInfo vert_shader_stage_info{
        .stage  = vk::ShaderStageFlagBits::eVertex,
        .module = shader_module,
        .pName  = "vertMain",
    };

    vk::PipelineShaderStageCreateInfo frag_shader_stage_info{
        .stage  = vk::ShaderStageFlagBits::eFragment,
        .module = shader_module,
        .pName  = "fragMain",
    };

    auto shader_stages = std::to_array({vert_shader_stage_info, frag_shader_stage_info});

    // Vertex Input State
    vk::PipelineVertexInputStateCreateInfo vertex_input_info{};

    // Input Assembly State
    vk::PipelineInputAssemblyStateCreateInfo input_assembly{
        .topology = vk::PrimitiveTopology::eTriangleList,
    };

    // Viewport State
    vk::Viewport viewport{
        .x        = 0.0f,
        .y        = 0.0f,
        .width    = static_cast<float>(swapchain_extent.width),
        .height   = static_cast<float>(swapchain_extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vk::Rect2D scissor{.offset = vk::Offset2D{.x = 0, .y = 0}, .extent = swapchain_extent};
    vk::PipelineViewportStateCreateInfo viewport_state{
        .viewportCount = 1,
        .pViewports    = &viewport,
        .scissorCount  = 1,
        .pScissors     = &scissor,
    };

    // Rasterization State
    vk::PipelineRasterizationStateCreateInfo rasterizer{
        .depthClampEnable        = vk::False,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode             = vk::PolygonMode::eFill,
        .cullMode                = vk::CullModeFlagBits::eBack,
        .frontFace               = vk::FrontFace::eClockwise,
        .depthBiasEnable         = vk::False,
        .lineWidth               = 1.0f,
    };

    // Multisampling
    vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable  = vk::False,
    };

    // Color Blending
    vk::PipelineColorBlendAttachmentState color_blend_attachment{
        .blendEnable    = vk::False,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eB |
                          vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eA,
    };
    std::vector<vk::PipelineColorBlendAttachmentState> color_blend_attachments = {
        color_blend_attachment,
    };
    vk::PipelineColorBlendStateCreateInfo color_blending{
        .logicOpEnable   = vk::False,
        .logicOp         = vk::LogicOp::eCopy,
        .attachmentCount = static_cast<std::uint32_t>(color_blend_attachments.size()),
        .pAttachments    = color_blend_attachments.data(),
    };

    // Pipeline Layout
    vk::PipelineLayoutCreateInfo pipeline_layout_info{
        .setLayoutCount         = 0,
        .pushConstantRangeCount = 0,
    };
    pipeline_layout = device.createPipelineLayout(pipeline_layout_info);

    // Specify Dynamic States
    std::vector<vk::DynamicState> dynamic_states = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };
    vk::PipelineDynamicStateCreateInfo dynamic_state{
        .dynamicStateCount = static_cast<std::uint32_t>(dynamic_states.size()),
        .pDynamicStates    = dynamic_states.data(),
    };

    // Pipeline Rendering Create Info
    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo>
        pipeline_create_info_chain = {
            {
                .stageCount          = 2,
                .pStages             = shader_stages.data(),
                .pVertexInputState   = &vertex_input_info,
                .pInputAssemblyState = &input_assembly,
                .pViewportState      = &viewport_state,
                .pRasterizationState = &rasterizer,
                .pMultisampleState   = &multisampling,
                .pColorBlendState    = &color_blending,
                .pDynamicState       = &dynamic_state,
                .layout              = pipeline_layout,
                .renderPass          = nullptr,
            },
            {
                .colorAttachmentCount    = 1,
                .pColorAttachmentFormats = &swapchain_surface_format.format,
            },
        };

    graphics_pipeline = device.createGraphicsPipeline(
        nullptr,
        pipeline_create_info_chain.get<vk::GraphicsPipelineCreateInfo>());
}

void GraphicLab::createCommandPool()
{
    vk::CommandPoolCreateInfo pool_info{
        .flags            = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = queue_index,
    };

    command_pool = device.createCommandPool(pool_info);
}

void GraphicLab::createCommandBuffer()
{
    vk::CommandBufferAllocateInfo alloc_info{
        .commandPool        = command_pool,
        .level              = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = kMaxFramesInFlight,
    };

    command_buffers = device.allocateCommandBuffers(alloc_info);
}

void GraphicLab::createSyncObjects()
{
    assert(present_complete_semaphores.empty() && render_finished_semaphores.empty());

    for (std::size_t i = 0; i < swapchain_images.size(); ++i)
    {
        render_finished_semaphores.emplace_back(device, vk::SemaphoreCreateInfo{});
    }

    for (std::size_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        present_complete_semaphores.emplace_back(device, vk::SemaphoreCreateInfo{});
        in_flight_fences.emplace_back(device, vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
    }
}

void GraphicLab::reacordCommandBuffer(std::uint32_t image_index)
{
    auto const& command_buffer = command_buffers[frame_index];

    command_buffer.begin({});

    transitionImageLayout(swapchain_images[image_index],
                          vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eColorAttachmentOptimal,
                          {}, // srcAccessMask (no need to wait for previous operations)
                          vk::AccessFlagBits2::eColorAttachmentWrite,         // dstAccessMask
                          vk::PipelineStageFlagBits2::eColorAttachmentOutput, // srcStage
                          vk::PipelineStageFlagBits2::eColorAttachmentOutput, // dstStage
                          command_buffer);

    vk::ClearValue clear_color                  = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
    vk::RenderingAttachmentInfo attachment_info = {
        .imageView   = swapchain_image_views[image_index],
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp      = vk::AttachmentLoadOp::eClear,
        .storeOp     = vk::AttachmentStoreOp::eStore,
        .clearValue  = clear_color,
    };

    vk::RenderingInfo rendering_info = {
        .renderArea           = {.offset = {.x = 0, .y = 0}, .extent = swapchain_extent},
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &attachment_info,
    };

    // Begining of Rendering
    command_buffer.beginRendering(rendering_info);

    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphics_pipeline);

    // Setup data for dynamic states
    command_buffer.setViewport(0,
                               vk::Viewport(0.0f,
                                            0.0f,
                                            static_cast<float>(swapchain_extent.width),
                                            static_cast<float>(swapchain_extent.height),
                                            0.0f,
                                            1.0f));
    command_buffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), swapchain_extent));

    command_buffer.draw(3, 1, 0, 0);

    command_buffer.endRendering();

    transitionImageLayout(swapchain_images[image_index],
                          vk::ImageLayout::eColorAttachmentOptimal,
                          vk::ImageLayout::ePresentSrcKHR,
                          vk::AccessFlagBits2::eColorAttachmentWrite,
                          {},
                          vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                          vk::PipelineStageFlagBits2::eBottomOfPipe,
                          command_buffer);

    command_buffer.end();
}

void GraphicLab::mainLoop()
{
    std::println("Start Drawing Frame...");
    while (!static_cast<bool>(glfwWindowShouldClose(window)))
    {
        glfwPollEvents();
        drawFrame();
    }
    device.waitIdle();
}

void GraphicLab::drawFrame()
{
    auto fence_result = device.waitForFences(*in_flight_fences[frame_index],
                                             vk::True,
                                             std::numeric_limits<std::uint64_t>::max());

    if (fence_result != vk::Result::eSuccess)
    {
        throw std::runtime_error("failed to wait for fence!");
    }

    device.resetFences(*in_flight_fences[frame_index]);

    auto [result, image_index] =
        swapchain.acquireNextImage(std::numeric_limits<std::uint64_t>::max(),
                                   *present_complete_semaphores[frame_index],
                                   nullptr);

    reacordCommandBuffer(image_index);

    vk::SemaphoreSubmitInfo present_complete_semaphore_submit_info{
        .semaphore = *present_complete_semaphores[frame_index],
        .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
    };
    vk::SemaphoreSubmitInfo render_finished_semaphore_submit_info{
        .semaphore = *render_finished_semaphores[image_index],
        .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
    };
    vk::CommandBufferSubmitInfo command_buffer_submit_info{
        .commandBuffer = *command_buffers[frame_index]
    };
    vk::SubmitInfo2 submit_info{
        .waitSemaphoreInfoCount   = 1,
        .pWaitSemaphoreInfos      = &present_complete_semaphore_submit_info,
        .commandBufferInfoCount   = 1,
        .pCommandBufferInfos      = &command_buffer_submit_info,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos    = &render_finished_semaphore_submit_info,
    };

    queue.submit2(submit_info, *in_flight_fences[frame_index]);

    vk::PresentInfoKHR const present_info_khr{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &*render_finished_semaphores[image_index],
        .swapchainCount     = 1,
        .pSwapchains        = &*swapchain,
        .pImageIndices      = &image_index,
    };

    result = queue.presentKHR(present_info_khr);

    frame_index = (frame_index + 1) % kMaxFramesInFlight;
}

void GraphicLab::run()
{
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
}

} // namespace gplab