# FindAPR.cmake - Find Apache Portable Runtime
#
# This module defines:
#   APR_FOUND - True if APR was found
#   APR_INCLUDE_DIRS - APR include directories
#   APR_LIBRARIES - APR libraries to link

find_package(PkgConfig QUIET)

if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_APR QUIET apr-1)
endif()

find_path(APR_INCLUDE_DIR
    NAMES apr.h
    HINTS
        ${PC_APR_INCLUDEDIR}
        ${PC_APR_INCLUDE_DIRS}
    PATH_SUFFIXES apr-1 apr-1.0 apr
)

find_library(APR_LIBRARY
    NAMES apr-1 apr
    HINTS
        ${PC_APR_LIBDIR}
        ${PC_APR_LIBRARY_DIRS}
)

# Handle version
if(PC_APR_VERSION)
    set(APR_VERSION_STRING ${PC_APR_VERSION})
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(APR
    REQUIRED_VARS APR_LIBRARY APR_INCLUDE_DIR
    VERSION_VAR APR_VERSION_STRING
)

if(APR_FOUND)
    set(APR_LIBRARIES ${APR_LIBRARY})
    set(APR_INCLUDE_DIRS ${APR_INCLUDE_DIR})
    
    if(NOT TARGET APR::APR)
        add_library(APR::APR UNKNOWN IMPORTED)
        set_target_properties(APR::APR PROPERTIES
            IMPORTED_LOCATION "${APR_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${APR_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(APR_INCLUDE_DIR APR_LIBRARY)
