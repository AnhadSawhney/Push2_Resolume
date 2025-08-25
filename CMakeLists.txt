cmake_minimum_required(VERSION 3.8)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project("push2_resolume")

INCLUDE_DIRECTORIES("${CMAKE_SOURCE_DIR}")

# Add GLFW
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(include/glfw)

# GLAD is header-only, just need to include the directory
# No library compilation needed

# NanoVG is header-only, just need to include the directory
# No library compilation needed

# Add all source files explicitly to avoid missing new .cpp files
set(SOURCES
    src/PushUI.cpp
    # Add other .cpp files here as needed
)
file(GLOB_RECURSE OTHER_SOURCES "src/*.cpp")
foreach(f ${OTHER_SOURCES})
    list(FIND SOURCES ${f} idx)
    if(idx EQUAL -1)
        list(APPEND SOURCES ${f})
    endif()
endforeach()
add_executable(push2_resolume ${SOURCES})

# Add pre-build step to kill executable if running
if(WIN32)
    add_custom_command(TARGET push2_resolume PRE_BUILD
        COMMAND taskkill /F /IM push2_resolume.exe /T 2>nul || echo Process not running
        COMMENT "Stopping running executable"
    )
else()
    add_custom_command(TARGET push2_resolume PRE_BUILD
        COMMAND bash -c "pkill -f push2_resolume 2>/dev/null || true"
        COMMENT "Stopping running executable"
    )
endif()

# Add RtMidi as a static library
add_library(rtmidi_static STATIC 
    include/RtMidi.cpp
)

# separate versions of NetworkingUtils.cpp and UdpSocket.cpp are provided for Win32 and POSIX
# the IpSystemTypePath selects the correct ones based on the current platform

IF(WIN32)
    set(IpSystemTypePath ip/win32)
    set(LIBS ${LIBS} Ws2_32 winmm)
    message(STATUS ">>> Windows")
    
    # Configure RtMidi for Windows
    target_compile_definitions(rtmidi_static PRIVATE __WINDOWS_MM__)
    
    # Link libraries
    target_link_libraries(push2_resolume ${CMAKE_CURRENT_SOURCE_DIR}/lib/libusb-1.0.lib)
    target_link_libraries(push2_resolume rtmidi_static winmm glfw nanovg opengl32)
    
ELSE(WIN32)
    set(IpSystemTypePath ip/posix)
    message(STATUS ">>> Linux")
    
    # Configure RtMidi for Linux (ALSA)
    target_compile_definitions(rtmidi_static PRIVATE __LINUX_ALSA__)
    
    add_library( libusb SHARED IMPORTED )
    set_target_properties( libusb PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/lib/libusb-1.0.so )
    target_link_libraries(push2_resolume libusb)
    target_link_libraries(push2_resolume rtmidi_static asound pthread glfw nanovg GL)
ENDIF(WIN32)


# Add oscpack as a static library (headers not needed in sources)
add_library(oscpack STATIC
    ip/IpEndpointName.cpp
    ${IpSystemTypePath}/NetworkingUtils.cpp
    ${IpSystemTypePath}/UdpSocket.cpp
    ip/PacketListener.h
    ip/TimerListener.h
    osc/OscTypes.cpp
    osc/OscHostEndianness.h
    osc/OscException.h
    osc/OscPacketListener.h
    osc/MessageMappingOscPacketListener.h
    osc/OscReceivedElements.cpp
    osc/OscPrintReceivedElements.h
    osc/OscPrintReceivedElements.cpp
    osc/OscOutboundPacketStream.cpp
)

TARGET_LINK_LIBRARIES(push2_resolume oscpack ${LIBS})


# Set C++ standard
if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET push2_resolume PROPERTY CXX_STANDARD 20)
endif()


# Include directories
target_include_directories(push2_resolume PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/glfw/include
    ${CMAKE_SOURCE_DIR}/include/nanovg/src
    ${CMAKE_SOURCE_DIR}/ip
    ${CMAKE_SOURCE_DIR}/osc
    ${CMAKE_SOURCE_DIR}/src
)

# Define NANOVG_GL3 for the main executable
target_compile_definitions(push2_resolume PRIVATE NANOVG_GL3)
target_include_directories(oscpack PRIVATE
    ${CMAKE_SOURCE_DIR}/ip
    ${CMAKE_SOURCE_DIR}/osc
)

target_include_directories(rtmidi_static PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_library(nanovg STATIC
    ${CMAKE_SOURCE_DIR}/include/nanovg/src/nanovg.c
    ${CMAKE_SOURCE_DIR}/src/nanovg_impl.cpp
)

target_include_directories(nanovg PRIVATE
    ${CMAKE_SOURCE_DIR}/include/nanovg/src
    ${CMAKE_SOURCE_DIR}/include
)

# Define NANOVG_GL3 for the nanovg library
target_compile_definitions(nanovg PRIVATE NANOVG_GL3)

if(MSVC)
  # Force to always compile with W4
  if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
    string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
  endif()
elseif(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
  # Update if necessary
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-long-long -pedantic")
endif()