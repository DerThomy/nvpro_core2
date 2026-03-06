# Window and GUI library.
set(LIB_NAME nvapp)

# Collect all source files in the utils directory
file(GLOB LIB_SOURCES "*.cpp" "*.hpp")
source_group("Source Files" FILES ${LIB_SOURCES})

# Define the utils library
add_library(${LIB_NAME} STATIC ${LIB_SOURCES})

# All libraries use nvpro_core2 as their include root
cmake_path(GET CMAKE_CURRENT_LIST_DIR PARENT_PATH PARENT_DIR)
target_include_directories(${LIB_NAME} PUBLIC ${PARENT_DIR})

target_link_libraries(
  ${LIB_NAME}
  PUBLIC
    nvutils
    nvvk
    nvgui
    glm    # Math library
    vma    # Vulkan Memory Allocator
    imgui  # Because GBuffer needs it (returns the ImTexture and uses ImGui_ImplVulkan_AddTexture
    implot # For Implot
    fmt    # For formatting strings
)

if(ANDROID)
  # if(NOT TARGET native_app_glue)
  #   add_library(native_app_glue STATIC
  #     ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
  #   target_include_directories(native_app_glue PUBLIC
  #     ${ANDROID_NDK}/sources/android/native_app_glue)
  # endif()
  target_link_libraries(${LIB_NAME} PUBLIC android log volk native_app_glue)
else()
  target_link_libraries(${LIB_NAME} PUBLIC glfw)
endif()

target_precompile_headers(${LIB_NAME} PRIVATE
  <filesystem>
)

add_library(nvpro2::${LIB_NAME} ALIAS ${LIB_NAME})
set_property(TARGET ${LIB_NAME} PROPERTY FOLDER "nvpro_core2")
