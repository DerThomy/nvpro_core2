# FindSlang.cmake
#
# Downloads the Slang SDK.
# You can use a custom installation instead by setting Slang_ROOT and Slang_VERSION.
#
# Sets the following variables:
# Slang_VERSION: The downloaded version of Slang.
# Slang_ROOT: Path to the Slang SDK root directory.
# Slang_INCLUDE_DIR: Directory that includes slang.h.
# Slang_SLANGC_EXECUTABLE: Path to the Slang compiler.
# Slang_LIBRARY: Linker library.
# Slang_DLL: Shared library.
# Slang_GLSL_MODULE: Shared library containing a precompiled GLSL module.
# Slang_GLSLANG: Shared library used for SLANG_OPTIMIZATION_LEVEL_HIGH.
#
# Creates an imported library, `Slang`, that can be linked against.
#
# Because Slang DLLs are dynamically loaded, samples will need to explicitly
# specify them in their copy_to_runtime_and_install calls if they use them, like this:
# copy_to_runtime_and_install(...
#   FILES ${Slang_GLSLANG} ${Slang_GLSL_MODULE} ${Slang_DLL}
#   PROGRAMS ${Slang_SLANGD_EXECUTABLE}
#   ...
# )

set(Slang_VERSION "2026.3.1" CACHE STRING "Slang version. If you change this and ran CMake before, you will need to delete the other Slang_* cache variables")

if(ANDROID)
  set(Slang_ROOT "${CMAKE_CURRENT_LIST_DIR}/../third_party/slang/build-android-arm64-v8a")
endif()

if(NOT Slang_ROOT)
  if(NOT ANDROID)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" ARCH_PROC)
  if(ARCH_PROC MATCHES "^(arm|aarch64)")
      if(WIN32)
          set(PACKMAN_ARCH "arm64")
      else()
          set(PACKMAN_ARCH "aarch64")
      endif()
      set(GITHUB_ARCH "aarch64")
  elseif(ARCH_PROC MATCHES "^(x86_64|amd64|i[3-6]86)")
      if(WIN32)
          set(PACKMAN_ARCH "x64")
      else()
          set(PACKMAN_ARCH "x86_64")
      endif()
      set(GITHUB_ARCH "x86_64")
  else()
      message(FATAL_ERROR "Unhandled architecture '${ARCH_PROC}'")
  endif()

  if(WIN32)
      set(SLANG_OS "windows")
  else()
      set(SLANG_OS "linux")
  endif()

  # Download Slang SDK.
  # We provide two URLs here since some users' proxies might break one or the other.
  # The "d4i3qtqj3r0z5.cloudfront.net" address is the public Omniverse Packman
  # server; it is not private.
  set(Slang_URLS
      "https://d4i3qtqj3r0z5.cloudfront.net/slang%40v${Slang_VERSION}-${SLANG_OS}-${PACKMAN_ARCH}-release.zip"
      "https://github.com/shader-slang/slang/releases/download/v${Slang_VERSION}/slang-${Slang_VERSION}-${SLANG_OS}-${GITHUB_ARCH}.zip"
  )

  include(DownloadPackage)
  download_package(
    NAME "Slang-${SLANG_OS}-${GITHUB_ARCH}"
    URLS ${Slang_URLS}
    VERSION ${Slang_VERSION}
    LOCATION Slang_SOURCE_DIR
  )

  # On Linux, the Cloudfront download of Slang might not have the executable bit
  # set on its executables and DLLs. This causes find_program to fail. To fix this,
  # call chmod a+rwx on those directories:
  if(UNIX)
    file(CHMOD_RECURSE ${Slang_SOURCE_DIR}/bin ${Slang_SOURCE_DIR}/lib
         FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_WRITE GROUP_EXECUTE WORLD_READ WORLD_WRITE WORLD_EXECUTE
    )
  endif()

  set(Slang_ROOT ${Slang_SOURCE_DIR} CACHE PATH "Path to the Slang SDK root directory")
  mark_as_advanced(Slang_ROOT)
  endif()
endif()

if(CMAKE_CROSSCOMPILING AND CMAKE_HOST_WIN32)
  if(NOT Slang_HOST_ROOT)
      set(HOST_SLANG_OS "windows")
      set(HOST_PACKMAN_ARCH "x64")
      set(HOST_GITHUB_ARCH "x86_64")

      set(Slang_Host_URLS
          "https://d4i3qtqj3r0z5.cloudfront.net/slang%40v${Slang_VERSION}-${HOST_SLANG_OS}-${HOST_PACKMAN_ARCH}-release.zip"
          "https://github.com/shader-slang/slang/releases/download/v${Slang_VERSION}/slang-${Slang_VERSION}-${HOST_SLANG_OS}-${HOST_GITHUB_ARCH}.zip"
      )

      include(DownloadPackage)
      download_package(
        NAME "Slang-${HOST_SLANG_OS}-${HOST_GITHUB_ARCH}"
        URLS ${Slang_Host_URLS}
        VERSION ${Slang_VERSION}
        LOCATION Slang_HOST_SOURCE_DIR
      )
      set(Slang_HOST_ROOT ${Slang_HOST_SOURCE_DIR} CACHE PATH "Path to the Host Slang SDK root directory")
  endif()
else()
  set(Slang_HOST_ROOT ${Slang_ROOT})
endif()

set(_Slang_LIB_PATH_SUFFIXES lib Release/lib Debug/lib RelWithDebInfo/lib Release Debug RelWithDebInfo)
set(_Slang_BIN_PATH_SUFFIXES bin lib Release/bin Release/lib Debug/bin Debug/lib RelWithDebInfo/bin RelWithDebInfo/lib Release Debug RelWithDebInfo)
if(CMAKE_BUILD_TYPE)
  list(INSERT _Slang_LIB_PATH_SUFFIXES 0 "${CMAKE_BUILD_TYPE}/lib" "${CMAKE_BUILD_TYPE}")
  list(INSERT _Slang_BIN_PATH_SUFFIXES 0 "${CMAKE_BUILD_TYPE}/bin" "${CMAKE_BUILD_TYPE}/lib" "${CMAKE_BUILD_TYPE}")
endif()

if(NOT Slang_INCLUDE_DIR)
find_path(Slang_INCLUDE_DIR
  slang.h
  HINTS ${Slang_ROOT}/include
        "${CMAKE_CURRENT_LIST_DIR}/../third_party/slang/include"
  NO_DEFAULT_PATH
  DOC "Directory that includes slang.h."
  NO_CMAKE_FIND_ROOT_PATH
)
mark_as_advanced(Slang_INCLUDE_DIR)
endif()

find_program(Slang_SLANGC_EXECUTABLE
  NAMES slangc 
  HINTS ${Slang_HOST_ROOT}/bin
  NO_DEFAULT_PATH
  DOC "Slang compiler (slangc)"
  NO_CMAKE_FIND_ROOT_PATH
)
mark_as_advanced(Slang_SLANGC_EXECUTABLE)

find_program(Slang_SLANGD_EXECUTABLE
  NAMES slangd
  HINTS ${Slang_HOST_ROOT}/bin
  NO_DEFAULT_PATH
  DOC "Slang language server (slangd)"
  NO_CMAKE_FIND_ROOT_PATH
)
mark_as_advanced(Slang_SLANGD_EXECUTABLE)

if(NOT Slang_LIBRARY)
find_library(Slang_LIBRARY
  NAMES slang-compiler slang
  HINTS ${Slang_ROOT}/lib ${Slang_ROOT}
  PATH_SUFFIXES ${_Slang_LIB_PATH_SUFFIXES}
  NO_DEFAULT_PATH
  DOC "Slang linker library"
  NO_CMAKE_FIND_ROOT_PATH
)
mark_as_advanced(Slang_LIBRARY)

if(WIN32)
  find_file(Slang_DLL
    NAMES slang-compiler.dll slang.dll
    HINTS ${Slang_ROOT}/bin
    NO_DEFAULT_PATH
    DOC "Slang shared library (.dll)"
    NO_CMAKE_FIND_ROOT_PATH
  )
else() # Unix; uses .so
  set(Slang_DLL ${Slang_LIBRARY} CACHE PATH "Slang shared library (.so)")
endif()
mark_as_advanced(Slang_DLL)
endif()

# CMake Import library
if(Slang_LIBRARY AND NOT TARGET Slang)
if(NOT TARGET Slang)
  add_library(Slang SHARED IMPORTED)
  set_target_properties(Slang PROPERTIES
                        IMPORTED_LOCATION ${Slang_DLL}
                        # NOTE(nbickford): Setting INTERFACE_INCLUDE_DIRECTORIES
                        # should make the include directory propagate upwards...
                        # but in CMake 3.31.6, it doesn't. In fact, it does the
                        # opposite; adding INTERFACE_INCLUDE_DIRECTORIES makes
                        # attempts to add it later have no effect.
                        # INTERFACE_INCLUDE_DIRECTORIES ${Slang_INCLUDE_DIR}
  )
  if(WIN32)
    set_property(TARGET Slang PROPERTY IMPORTED_IMPLIB ${Slang_LIBRARY})
  else()
    # Vulkan SDK includes 'libslang.so' and sets LD_LIBRARY_PATH, which conflict
    # with the downloaded slang. This uses the deprecated RPATH instead of
    # RUNPATH to take priority over LD_LIBRARY_PATH.
    set_target_properties(Slang PROPERTIES
      INTERFACE_LINK_OPTIONS "-Wl,--disable-new-dtags"
    )
  endif()
endif()
endif()

# If we want to use Slang with .enableGLSL = true, then we should copy the Slang
# GLSL module to the output directory as well. Otherwise, Slang might use the
# slang-glsl-module.dll under the Vulkan SDK directory (if the Vulkan SDK is
# on PATH), which may be incompatible.
# To make this work, we make the GLSL module an IMPORTED library, with the same
# IMPLIB as core Slang.
find_file(Slang_GLSL_MODULE
  NAMES ${CMAKE_SHARED_LIBRARY_PREFIX}slang-glsl-module-${Slang_VERSION}${CMAKE_SHARED_LIBRARY_SUFFIX}
        ${CMAKE_SHARED_LIBRARY_PREFIX}slang-glsl-module${CMAKE_SHARED_LIBRARY_SUFFIX}
  HINTS ${Slang_ROOT}/bin
        ${Slang_ROOT}/lib
        ${Slang_ROOT}/${CMAKE_BUILD_TYPE}/bin
        ${Slang_ROOT}/${CMAKE_BUILD_TYPE}/lib
  PATH_SUFFIXES ${_Slang_BIN_PATH_SUFFIXES}
  NO_DEFAULT_PATH
  DOC "Slang embedded GLSL module"
  NO_CMAKE_FIND_ROOT_PATH
)
mark_as_advanced(Slang_GLSL_MODULE)

if(NOT TARGET SlangGlslModule)
  add_library(SlangGlslModule SHARED IMPORTED)
  set_target_properties(SlangGlslModule PROPERTIES
    IMPORTED_NO_SONAME ON # See https://github.com/shader-slang/slang/issues/7722
    IMPORTED_LOCATION ${Slang_GLSL_MODULE}
  )
  if(WIN32)
    set_property(TARGET SlangGlslModule PROPERTY IMPORTED_IMPLIB ${Slang_LIBRARY})
  endif()
endif()

# Additionally, SLANG_OPTIMIZATION_LEVEL_HIGH requires slang-glslang.dll.
# Find it:
find_file(Slang_GLSLANG
  NAMES ${CMAKE_SHARED_LIBRARY_PREFIX}slang-glslang-${Slang_VERSION}${CMAKE_SHARED_LIBRARY_SUFFIX}
        ${CMAKE_SHARED_LIBRARY_PREFIX}slang-glslang${CMAKE_SHARED_LIBRARY_SUFFIX}
  HINTS ${Slang_ROOT}/bin
        ${Slang_ROOT}/lib
        ${Slang_ROOT}/${CMAKE_BUILD_TYPE}/bin
        ${Slang_ROOT}/${CMAKE_BUILD_TYPE}/lib
  PATH_SUFFIXES ${_Slang_BIN_PATH_SUFFIXES}
  NO_DEFAULT_PATH
  DOC "slang-glslang shared library"
  NO_CMAKE_FIND_ROOT_PATH
)
mark_as_advanced(Slang_GLSLANG)

if(NOT TARGET SlangGlslang)
  add_library(SlangGlslang SHARED IMPORTED)
  set_target_properties(SlangGlslang PROPERTIES
    IMPORTED_NO_SONAME ON # See https://github.com/shader-slang/slang/issues/7722
    IMPORTED_LOCATION ${Slang_GLSLANG}
  )
  if(WIN32)
    set_property(TARGET SlangGlslang PROPERTY IMPORTED_IMPLIB ${Slang_LIBRARY})
  endif()
endif()


message(WARNING "--> using SLANGC under: ${Slang_SLANGC_EXECUTABLE}")
message(WARNING "--> using SLANG LIB: ${Slang_LIBRARY}")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Slang
  REQUIRED_VARS
    Slang_SLANGC_EXECUTABLE
    Slang_LIBRARY
    Slang_DLL
    Slang_INCLUDE_DIR
  VERSION_VAR
    Slang_VERSION
)
