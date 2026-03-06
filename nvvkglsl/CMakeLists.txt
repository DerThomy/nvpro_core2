# nvcompilers links with the Slang and ShaderC runtime compilers,
# and includers helpers for using these runtime compilers.

set(LIB_NAME nvvkglsl)

# Collect all source files
file(GLOB LIB_SOURCES "*.cpp" "*.hpp")
source_group("Source Files" FILES ${LIB_SOURCES})

# Define the library
add_library(${LIB_NAME} STATIC ${LIB_SOURCES})

# All libraries use nvpro_core2 as their include root
cmake_path(GET CMAKE_CURRENT_LIST_DIR PARENT_PATH PARENT_DIR)
target_include_directories(${LIB_NAME} PUBLIC ${PARENT_DIR})

find_package(Vulkan REQUIRED)

target_link_libraries(
  ${LIB_NAME}
  PUBLIC nvutils
         nvvk
)

if(ANDROID)
  target_include_directories(${LIB_NAME} PUBLIC ${ANDROID_NDK}/sources/third_party/shaderc/include)
  target_link_libraries(${LIB_NAME} PUBLIC ${ANDROID_NDK}/sources/third_party/shaderc/libs/c++_static/${ANDROID_ABI}/libshaderc.a )
else()
  if(TARGET Vulkan::shaderc_combined)
    target_link_libraries(${LIB_NAME} PUBLIC Vulkan::shaderc_combined)
  else()
    target_link_libraries(${LIB_NAME} PUBLIC shaderc_shared)
  endif()
endif()

target_precompile_headers(${LIB_NAME} PRIVATE
  <filesystem>
)

add_library(nvpro2::${LIB_NAME} ALIAS ${LIB_NAME})
set_property(TARGET ${LIB_NAME} PROPERTY FOLDER "nvpro_core2")
