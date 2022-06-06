find_package(Libva REQUIRED)

find_path(MSDK_INCLUDE_DIR NAMES mfx/mfxenc.h PATHS "${MSDK_ROOT_DIR}/include" NO_DEFAULT_PATH)
find_library(MSDK_LIBRARY NAMES mfx PATHS "${MSDK_ROOT_DIR}/lib" NO_DEFAULT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Msdk DEFAULT_MSG MSDK_LIBRARY MSDK_INCLUDE_DIR)

mark_as_advanced(MSDK_INCLUDE_DIR MSDK_LIBRARY)

if(Msdk_FOUND)
  if(NOT TARGET Msdk::mfx)
    add_library(Msdk::mfx UNKNOWN IMPORTED)

    target_link_libraries(Msdk::mfx INTERFACE Libva::va Libva::va_drm)
    set_target_properties(Msdk::mfx PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${MSDK_INCLUDE_DIR}"
      IMPORTED_LOCATION "${MSDK_LIBRARY}")
  endif()
endif()
