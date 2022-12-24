find_path(Lyra_INCLUDE_DIR NAMES lyra.h PATHS "${LYRA_DIR}/include")
find_library(Lyra_LIBRARY NAMES lyra PATHS "${LYRA_DIR}/lib")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Lyra DEFAULT_MSG Lyra_LIBRARY Lyra_INCLUDE_DIR)

mark_as_advanced(Lyra_INCLUDE_DIR Lyra_LIBRARY)

if(Lyra_FOUND)
  if(NOT TARGET Lyra::lyra)
    add_library(Lyra::lyra UNKNOWN IMPORTED)

    set(_DIRS ${Lyra_INCLUDE_DIR})

    set_target_properties(Lyra::lyra PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${_DIRS}"
      IMPORTED_LOCATION "${Lyra_LIBRARY}")
  endif()
endif()
