# FindSofia.cmake - Find Sofia-SIP library
#
# This module defines:
#   Sofia_FOUND - True if Sofia-SIP was found
#   Sofia_INCLUDE_DIRS - Sofia-SIP include directories
#   Sofia_LIBRARIES - Sofia-SIP libraries to link

find_package(PkgConfig QUIET)

if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_SOFIA QUIET sofia-sip-ua)
endif()

find_path(Sofia_INCLUDE_DIR
    NAMES sofia-sip/sip.h
    HINTS
        ${PC_SOFIA_INCLUDEDIR}
        ${PC_SOFIA_INCLUDE_DIRS}
    PATHS
        /usr/include
    PATH_SUFFIXES sofia-sip-1.12 sofia-sip-1.13
)

find_library(Sofia_LIBRARY
    NAMES sofia-sip-ua
    HINTS
        ${PC_SOFIA_LIBDIR}
        ${PC_SOFIA_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Sofia
    REQUIRED_VARS Sofia_LIBRARY Sofia_INCLUDE_DIR
)

if(Sofia_FOUND)
    set(Sofia_LIBRARIES ${Sofia_LIBRARY})
    set(Sofia_INCLUDE_DIRS ${Sofia_INCLUDE_DIR})
    
    if(NOT TARGET Sofia::Sofia)
        add_library(Sofia::Sofia UNKNOWN IMPORTED)
        set_target_properties(Sofia::Sofia PROPERTIES
            IMPORTED_LOCATION "${Sofia_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${Sofia_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(Sofia_INCLUDE_DIR Sofia_LIBRARY)
