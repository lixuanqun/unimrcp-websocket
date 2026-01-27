# FindAPRUtil.cmake - Find Apache Portable Runtime Utilities
#
# This module defines:
#   APRUtil_FOUND - True if APR-Util was found
#   APRUtil_INCLUDE_DIRS - APR-Util include directories
#   APRUtil_LIBRARIES - APR-Util libraries to link

find_package(PkgConfig QUIET)

if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_APRUTIL QUIET apr-util-1)
endif()

find_path(APRUtil_INCLUDE_DIR
    NAMES apu.h
    HINTS
        ${PC_APRUTIL_INCLUDEDIR}
        ${PC_APRUTIL_INCLUDE_DIRS}
    PATH_SUFFIXES apr-1 apr-1.0 apr
)

find_library(APRUtil_LIBRARY
    NAMES aprutil-1 aprutil
    HINTS
        ${PC_APRUTIL_LIBDIR}
        ${PC_APRUTIL_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(APRUtil
    REQUIRED_VARS APRUtil_LIBRARY APRUtil_INCLUDE_DIR
)

if(APRUtil_FOUND)
    set(APRUtil_LIBRARIES ${APRUtil_LIBRARY})
    set(APRUtil_INCLUDE_DIRS ${APRUtil_INCLUDE_DIR})
    
    if(NOT TARGET APRUtil::APRUtil)
        add_library(APRUtil::APRUtil UNKNOWN IMPORTED)
        set_target_properties(APRUtil::APRUtil PROPERTIES
            IMPORTED_LOCATION "${APRUtil_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${APRUtil_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(APRUtil_INCLUDE_DIR APRUtil_LIBRARY)
