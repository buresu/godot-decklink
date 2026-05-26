cmake_minimum_required(VERSION 3.15)

project(godot-decklink VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/Modules")

if(CMAKE_GENERATOR_PLATFORM MATCHES "arm64|aarch64|ARM64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
    set(ARCH "arm64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH "x86_64")
else()
    set(ARCH "x86_32")
endif()

set(GODOT_CPP_PATH "${PROJECT_SOURCE_DIR}/lib/godot-cpp")
if(NOT EXISTS "${GODOT_CPP_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "godot-cpp submodule not found. Run: git submodule update --init --recursive")
endif()
add_subdirectory("${GODOT_CPP_PATH}" "${CMAKE_BINARY_DIR}/godot-cpp")

find_package(DeckLink REQUIRED)

file(GLOB_RECURSE INCLUDES CONFIGURE_DEPENDS "src/*.hpp")
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_library(GodotDeckLink SHARED ${SOURCES} ${INCLUDES})

set_target_properties(GodotDeckLink PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "godot-decklink.$<IF:$<PLATFORM_ID:Windows>,windows,$<IF:$<PLATFORM_ID:Darwin>,macos,linux>>.$<IF:$<CONFIG:Debug>,debug,release>.${ARCH}"
)

target_include_directories(GodotDeckLink PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    ${DECKLINK_INCLUDE_DIRS}
    "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(GodotDeckLink PRIVATE godot::cpp ${DECKLINK_LIBS})
if(UNIX AND NOT APPLE)
    target_link_libraries(GodotDeckLink PRIVATE dl pthread)
endif()
if(WIN32)
    target_link_libraries(GodotDeckLink PRIVATE ole32)
endif()

set_target_properties(GodotDeckLink PROPERTIES
    SKIP_RPATH TRUE
    BUILD_RPATH_USE_ORIGIN TRUE
    SKIP_BUILD_RPATH FALSE
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH_USE_LINK_PATH FALSE)
if(APPLE)
    set_target_properties(GodotDeckLink PROPERTIES INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(GodotDeckLink PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()

install(TARGETS GodotDeckLink
    RUNTIME DESTINATION "${PROJECT_SOURCE_DIR}/demo/addons/godot-decklink/bin"
    LIBRARY DESTINATION "${PROJECT_SOURCE_DIR}/demo/addons/godot-decklink/bin"
)
