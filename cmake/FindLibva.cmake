find_package(PkgConfig)
pkg_check_modules(Libdrm libdrm)
if(LIBDRM_FOUND)
  if(NOT TARGET Libdrm::drm)
    add_library(Libdrm::drm UNKNOWN IMPORTED)

    set_target_properties(Libdrm::drm PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${LIBDRM_INCLUDE_DIRS}"
      IMPORTED_LOCATION "${LIBDRM_LINK_LIBRARIES}")
  endif()
endif()

find_path(LIBVA_INCLUDE_DIR NAMES va/va.h PATHS "${LIBVA_ROOT_DIR}/include" NO_DEFAULT_PATH)
find_library(LIBVA_LIBRARY NAMES va PATHS "${LIBVA_ROOT_DIR}/lib" NO_DEFAULT_PATH)
find_library(LIBVA_DRM_LIBRARY NAMES va-drm PATHS "${LIBVA_ROOT_DIR}/lib" NO_DEFAULT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LIBVA DEFAULT_MSG LIBVA_INCLUDE_DIR LIBVA_LIBRARY LIBVA_DRM_LIBRARY)

mark_as_advanced(LIBVA_INCLUDE_DIR LIBVA_LIBRARY LIBVA_DRM_LIBRARY)

if(LIBVA_FOUND)
  if(NOT TARGET Libva::va)
    add_library(Libva::va UNKNOWN IMPORTED)

    set_target_properties(Libva::va PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${LIBVA_INCLUDE_DIR}"
      IMPORTED_LOCATION "${LIBVA_LIBRARY}")
  endif()
  if(NOT TARGET Libva::va_drm)
    add_library(Libva::va_drm UNKNOWN IMPORTED)

    target_link_libraries(Libva::va_drm INTERFACE Libdrm::drm)
    set_target_properties(Libva::va_drm PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${LIBVA_INCLUDE_DIR}"
      IMPORTED_LOCATION "${LIBVA_DRM_LIBRARY}")
  endif()
endif()
