# WebSocket Synthesizer Plugin

cmake_minimum_required(VERSION 3.0)
project(websocketsynth C)

# Source files
set(WEBSOCKETSYNTH_SOURCES
    src/websocket_synth_engine.c
)

# Create plugin module
add_library(websocketsynth MODULE ${WEBSOCKETSYNTH_SOURCES})

# Include directories
target_include_directories(websocketsynth PRIVATE
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
target_link_libraries(websocketsynth
    mrcpengine
    mrcp
    mpf
    aprtoolkit
    websocketclient
    ${APR_LIBRARIES}
    ${APRUtil_LIBRARIES}
)

# Set output properties
set_target_properties(websocketsynth PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
)

# Install
install(TARGETS websocketsynth
    LIBRARY DESTINATION lib/unimrcp
)
