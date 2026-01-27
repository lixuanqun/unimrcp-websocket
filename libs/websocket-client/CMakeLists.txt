# WebSocket Client Library for UniMRCP Plugins

cmake_minimum_required(VERSION 3.0)
project(websocketclient C)

# Source files
set(WEBSOCKETCLIENT_SOURCES
    src/ws_client.c
)

# Header files
set(WEBSOCKETCLIENT_HEADERS
    include/ws_client.h
)

# Create library
add_library(websocketclient STATIC ${WEBSOCKETCLIENT_SOURCES})

# Include directories
target_include_directories(websocketclient PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/libs/apr-toolkit/include
)

# Link dependencies
target_link_libraries(websocketclient
    aprtoolkit
    ${APR_LIBRARIES}
    ${APRUtil_LIBRARIES}
)

# Install
install(TARGETS websocketclient
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(FILES ${WEBSOCKETCLIENT_HEADERS}
    DESTINATION include
)
