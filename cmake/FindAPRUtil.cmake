# Find APR-Util library
# This module finds the Apache Portable Runtime Utility library
#
# It sets the following variables:
#   APRUtil_FOUND        - True if APRUtil was found
#   APRUtil_INCLUDE_DIRS - APRUtil include directories
#   APRUtil_LIBRARIES    - APRUtil libraries

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_APRUTIL QUIET apr-util-1)
endif()

find_path(APRUtil_INCLUDE_DIR
    NAMES apu.h
    PATHS
        ${PC_APRUTIL_INCLUDE_DIRS}
        /usr/include/apr-1.0
        /usr/include/apr-1
        /usr/local/include/apr-1.0
        /usr/local/include/apr-1
        /opt/apr/include/apr-1
    PATH_SUFFIXES apr-1.0 apr-1
)

find_library(APRUtil_LIBRARY
    NAMES aprutil-1 aprutil
    PATHS
        ${PC_APRUTIL_LIBRARY_DIRS}
        /usr/lib
        /usr/lib/x86_64-linux-gnu
        /usr/local/lib
        /opt/apr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(APRUtil
    REQUIRED_VARS APRUtil_LIBRARY APRUtil_INCLUDE_DIR
)

if(APRUtil_FOUND)
    set(APRUtil_LIBRARIES ${APRUtil_LIBRARY})
    set(APRUtil_INCLUDE_DIRS ${APRUtil_INCLUDE_DIR})
endif()

mark_as_advanced(APRUtil_INCLUDE_DIR APRUtil_LIBRARY)
