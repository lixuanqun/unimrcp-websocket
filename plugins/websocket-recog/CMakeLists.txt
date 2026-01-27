# WebSocket Recognizer Plugin

cmake_minimum_required(VERSION 3.0)
project(websocketrecog C)

# Source files
set(WEBSOCKETRECOG_SOURCES
    src/websocket_recog_engine.c
)

# Create plugin module
add_library(websocketrecog MODULE ${WEBSOCKETRECOG_SOURCES})

# Include directories
target_include_directories(websocketrecog PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/mrcp-engine/include
    ${CMAKE_SOURCE_DIR}/libs/mrcp/include
    ${CMAKE_SOURCE_DIR}/libs/mrcp/message/include
    ${CMAKE_SOURCE_DIR}/libs/mrcp/control/include
    ${CMAKE_SOURCE_DIR}/libs/mrcp/resources/include
    ${CMAKE_SOURCE_DIR}/libs/mpf/include
    ${CMAKE_SOURCE_DIR}/libs/apr-toolkit/include
    ${CMAKE_SOURCE_DIR}/libs/websocket-client/include
)

# Link libraries
target_link_libraries(websocketrecog
    mrcpengine
    mrcp
    mpf
    aprtoolkit
    websocketclient
    ${APR_LIBRARIES}
    ${APRUtil_LIBRARIES}
)

# Set output properties
set_target_properties(websocketrecog PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
)

# Install
install(TARGETS websocketrecog
    LIBRARY DESTINATION ${UNIMRCP_PLUGIN_DIR}
)
