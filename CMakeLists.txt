cmake_minimum_required(VERSION 3.31)

if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)

    set(
        CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
        "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>"
    )
endif()

project(
    graphic-lab
    LANGUAGES CXX
)

option(
    GRAPHIC_LAB_WARNINGS_AS_ERRORS
    "Treat project warnings as errors"
    ON
)

option(
    GRAPHIC_LAB_ENABLE_CLANG_TIDY
    "Run clang-tidy during compilation"
    ON
)

option(
    GRAPHIC_LAB_ENABLE_MSVC_ANALYZE
    "Enable Microsoft static code analysis"
    OFF
)

#
# Common project options
#

add_library(graphic_lab_project_options INTERFACE)

add_library(
    graphic-lab::project-options
    ALIAS
    graphic_lab_project_options
)

target_compile_features(
    graphic_lab_project_options
    INTERFACE
        cxx_std_23
)

target_compile_definitions(
    graphic_lab_project_options
    INTERFACE
        VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1

        VMA_STATIC_VULKAN_FUNCTIONS=0
        VMA_DYNAMIC_VULKAN_FUNCTIONS=1

        "$<$<PLATFORM_ID:Windows>:NOMINMAX>"
        "$<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>"
)

#
# Compiler warnings
#

add_library(graphic_lab_project_warnings INTERFACE)

add_library(
    graphic-lab::project-warnings
    ALIAS
    graphic_lab_project_warnings
)

target_compile_options(
    graphic_lab_project_warnings
    INTERFACE
        # MSVC
        "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W4>"
        "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->"
        "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/sdl>"

        # GCC
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wall>"
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wextra>"
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wpedantic>"
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wconversion>"
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wsign-conversion>"
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wshadow>"
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wnon-virtual-dtor>"
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Woverloaded-virtual>"

        # Clang
        "$<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-Wall>"
        "$<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-Wextra>"
        "$<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-Wpedantic>"
        "$<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-Wconversion>"
        "$<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-Wsign-conversion>"
        "$<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-Wshadow>"
        "$<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-Wnon-virtual-dtor>"
        "$<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-Woverloaded-virtual>"
)

#
# Static-analysis discovery
#

if(GRAPHIC_LAB_ENABLE_CLANG_TIDY)
    find_program(
        GRAPHIC_LAB_CLANG_TIDY_EXECUTABLE
        NAMES clang-tidy
        REQUIRED
    )
endif()

#
# Apply project-local settings to a target
#

function(graphic_lab_apply_project_settings target_name)
    if(NOT TARGET "${target_name}")
        message(
            FATAL_ERROR
            "graphic_lab_apply_project_settings(): "
            "target '${target_name}' does not exist"
        )
    endif()

    target_link_libraries(
        "${target_name}"
        PRIVATE
            graphic-lab::project-options
            graphic-lab::project-warnings
    )

    set_property(
        TARGET "${target_name}"
        PROPERTY COMPILE_WARNING_AS_ERROR
        "${GRAPHIC_LAB_WARNINGS_AS_ERRORS}"
    )

    if(GRAPHIC_LAB_ENABLE_CLANG_TIDY)
        set_property(
            TARGET "${target_name}"
            PROPERTY CXX_CLANG_TIDY
                "${GRAPHIC_LAB_CLANG_TIDY_EXECUTABLE}"
                "--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy"
                "$<$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>:--extra-arg=/EHsc>"
        )
    endif()

    if(GRAPHIC_LAB_ENABLE_MSVC_ANALYZE AND MSVC)
        target_compile_options(
            "${target_name}"
            PRIVATE
                /analyze
                /analyze:external-
        )
    endif()
endfunction()

add_subdirectory(src)