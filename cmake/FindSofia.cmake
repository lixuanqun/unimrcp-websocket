# Find Sofia-SIP library
# This module finds the Sofia-SIP library
#
# It sets the following variables:
#   Sofia_FOUND        - True if Sofia was found
#   Sofia_INCLUDE_DIRS - Sofia include directories
#   Sofia_LIBRARIES    - Sofia libraries

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_SOFIA QUIET sofia-sip-ua)
endif()

# Look for the sofia-sip directory that contains sofia-sip/sip.h
find_path(Sofia_INCLUDE_DIR
    NAMES sofia-sip/sip.h
    PATHS
        ${PC_SOFIA_INCLUDE_DIRS}
        /usr/include/sofia-sip-1.12
        /usr/include/sofia-sip-1.13
        /usr/include
        /usr/local/include/sofia-sip-1.12
        /usr/local/include/sofia-sip-1.13
        /usr/local/include
        /opt/sofia-sip/include
    NO_DEFAULT_PATH
)

# Fallback search
if(NOT Sofia_INCLUDE_DIR)
    find_path(Sofia_INCLUDE_DIR
        NAMES sofia-sip/sip.h
    )
endif()

find_library(Sofia_LIBRARY
    NAMES sofia-sip-ua
    PATHS
        ${PC_SOFIA_LIBRARY_DIRS}
        /usr/lib
        /usr/lib/x86_64-linux-gnu
        /usr/local/lib
        /opt/sofia-sip/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Sofia
    REQUIRED_VARS Sofia_LIBRARY Sofia_INCLUDE_DIR
)

if(Sofia_FOUND)
    set(Sofia_LIBRARIES ${Sofia_LIBRARY})
    set(Sofia_INCLUDE_DIRS ${Sofia_INCLUDE_DIR})
endif()

mark_as_advanced(Sofia_INCLUDE_DIR Sofia_LIBRARY)
