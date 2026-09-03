#
# Shader compiling function
#
function(add_slang_shader_target TARGET)
    cmake_parse_arguments(
        SHADER
        ""
        ""
        "SOURCES"
        ${ARGN}
    )

    set(OUTPUT_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    )

    set(OUTPUT_FILE
        "${OUTPUT_DIR}/slang.spv"
    )

    set(ENTRY_POINTS
        -entry vertMain
        -entry fragMain
    )

    find_program(
        SLANGC_EXECUTABLE
        NAMES slangc
        REQUIRED
    )

    add_custom_command(
        OUTPUT "${OUTPUT_FILE}"

        COMMAND
            ${CMAKE_COMMAND}
            -E make_directory
            "${OUTPUT_DIR}"

        COMMAND
            "${SLANGC_EXECUTABLE}"
            ${SHADER_SOURCES}
            -target spirv
            -profile spirv_1_4
            -emit-spirv-directly
            -fvk-use-entrypoint-name
            -o "${OUTPUT_FILE}"

        COMMAND
            ${CMAKE_COMMAND}
            -E sha256sum
            "${OUTPUT_FILE}"

        DEPENDS
            ${SHADER_SOURCES}

        COMMENT "Compiling Slang shaders"
        VERBATIM
    )

    add_custom_target(
        ${TARGET}
        DEPENDS "${OUTPUT_FILE}"
    )
endfunction()

find_package(glm CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

add_library(
    graphic-lab
    "graphic-lab.cpp"
)

target_include_directories(
    graphic-lab
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(
    graphic-lab
    PUBLIC
        Vulkan::Headers
    PRIVATE
        GPUOpen::VulkanMemoryAllocator
        glfw
        glm::glm
)

add_slang_shader_target(
    shader 
    SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.slang"
)

graphic_lab_apply_project_settings(graphic-lab)
