set(_DECKLINK_SEARCH_ROOTS)

if(DEFINED DECKLINK_SDK_DIR)
    list(APPEND _DECKLINK_SEARCH_ROOTS "${DECKLINK_SDK_DIR}")
endif()
if(DEFINED ENV{DECKLINK_SDK_DIR})
    list(APPEND _DECKLINK_SEARCH_ROOTS "$ENV{DECKLINK_SDK_DIR}")
endif()

get_filename_component(_DECKLINK_PROJECT_PARENT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
list(APPEND _DECKLINK_SEARCH_ROOTS "${_DECKLINK_PROJECT_PARENT}/BlackmagicDeckLinkSDK")

if(WIN32)
    set(_DECKLINK_PLATFORM_INCLUDE_SUFFIXES "Win/include" "include")
elseif(APPLE)
    set(_DECKLINK_PLATFORM_INCLUDE_SUFFIXES "Mac/include" "include")
else()
    set(_DECKLINK_PLATFORM_INCLUDE_SUFFIXES "Linux/include" "include")
endif()

find_path(DECKLINK_INCLUDE_DIR
    NAMES DeckLinkAPIVersion.h
    HINTS ${_DECKLINK_SEARCH_ROOTS}
    PATH_SUFFIXES ${_DECKLINK_PLATFORM_INCLUDE_SUFFIXES}
)

if(DECKLINK_INCLUDE_DIR)
    get_filename_component(DECKLINK_DIR "${DECKLINK_INCLUDE_DIR}/.." ABSOLUTE)
    set(DECKLINK_INCLUDE_DIRS "${DECKLINK_INCLUDE_DIR}")
    set(DECKLINK_FOUND TRUE)
else()
    set(DECKLINK_FOUND FALSE)
endif()

if(DECKLINK_FOUND)
    if(WIN32)
        find_program(MIDL_EXECUTABLE midl REQUIRED)

        set(DECKLINK_MIDL_HEADER "${CMAKE_CURRENT_BINARY_DIR}/DeckLinkAPI.h")
        set(DECKLINK_MIDL_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/DeckLinkAPI_i.c")
        set(DECKLINK_MIDL_FILE "${DECKLINK_INCLUDE_DIR}/DeckLinkAPI.idl")

        add_custom_command(
            OUTPUT "${DECKLINK_MIDL_HEADER}" "${DECKLINK_MIDL_SOURCE}"
            COMMAND "${MIDL_EXECUTABLE}" /h DeckLinkAPI.h "${DECKLINK_MIDL_FILE}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            DEPENDS "${DECKLINK_MIDL_FILE}"
            VERBATIM)

        set_source_files_properties("${DECKLINK_MIDL_HEADER}" "${DECKLINK_MIDL_SOURCE}"
            PROPERTIES GENERATED TRUE)
        set(DECKLINK_INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}" "${DECKLINK_INCLUDE_DIR}")
        set(DECKLINK_SOURCES "${DECKLINK_MIDL_SOURCE}")
    else()
        set(DECKLINK_SOURCES "${DECKLINK_INCLUDE_DIR}/DeckLinkAPIDispatch.cpp")
    endif()

    if(NOT TARGET DeckLink)
        add_library(DeckLink STATIC ${DECKLINK_SOURCES})
        target_include_directories(DeckLink PUBLIC ${DECKLINK_INCLUDE_DIRS})
        set_target_properties(DeckLink PROPERTIES POSITION_INDEPENDENT_CODE ON)
        if(WIN32)
            set_target_properties(DeckLink PROPERTIES LINKER_LANGUAGE C)
        endif()
    endif()

    set(DECKLINK_LIBS DeckLink)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(DeckLink
    REQUIRED_VARS DECKLINK_INCLUDE_DIR DECKLINK_FOUND
)

mark_as_advanced(DECKLINK_INCLUDE_DIR)
