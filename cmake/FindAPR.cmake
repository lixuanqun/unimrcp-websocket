# Find APR library
# This module finds the Apache Portable Runtime library
#
# It sets the following variables:
#   APR_FOUND        - True if APR was found
#   APR_INCLUDE_DIRS - APR include directories
#   APR_LIBRARIES    - APR libraries

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_APR QUIET apr-1)
endif()

find_path(APR_INCLUDE_DIR
    NAMES apr.h
    PATHS
        ${PC_APR_INCLUDE_DIRS}
        /usr/include/apr-1.0
        /usr/include/apr-1
        /usr/local/include/apr-1.0
        /usr/local/include/apr-1
        /opt/apr/include/apr-1
    PATH_SUFFIXES apr-1.0 apr-1
)

find_library(APR_LIBRARY
    NAMES apr-1 apr
    PATHS
        ${PC_APR_LIBRARY_DIRS}
        /usr/lib
        /usr/lib/x86_64-linux-gnu
        /usr/local/lib
        /opt/apr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(APR
    REQUIRED_VARS APR_LIBRARY APR_INCLUDE_DIR
)

if(APR_FOUND)
    set(APR_LIBRARIES ${APR_LIBRARY})
    set(APR_INCLUDE_DIRS ${APR_INCLUDE_DIR})
endif()

mark_as_advanced(APR_INCLUDE_DIR APR_LIBRARY)
