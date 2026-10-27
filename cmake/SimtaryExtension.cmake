# SimtaryExtension.cmake - extensions: source trees that add framework-level code to a game
# without the engine knowing about them. The first one is Simtary, the simulation layer that
# sits on top of Stratary (flight dynamics on JSBSim, and later sensors, optics, ROS2...).
#
# An extension is a directory whose CMakeLists.txt calls
#
#   stratary_define_extension(NAME <name>
#       [FRAMEWORK_DIR <dir>]       # compiled into each app WITH the framework (default <ext>/Framework)
#       [LIBS <targets...>]         # linked to every app that uses it
#       [RUNTIME <targets...>]      # shared libraries copied next to the app's executable
#   )
#
# A game pulls it in after the bootstrap and names it when it adds itself:
#
#   stratary_add_extension(${SIMTARY_EXT_ROOT})
#   simtary_add_app(NAME ... EXTENSIONS Simtary)
#
# The sources join the FRAMEWORK's, not the game's: native components register themselves at
# static init, and in a MODULE build they have to live in the host with the rest of the
# framework. The dependency only goes one way - Stratary's Framework/ never includes an
# extension header (simtary_add_app checks), so a game without the extension still builds.

include_guard(GLOBAL)

# Add an extension tree to the build, once per configure however many apps ask for it.
function(stratary_add_extension dir)
    get_filename_component(dir "${dir}" ABSOLUTE)
    if (NOT EXISTS "${dir}/CMakeLists.txt")
        message(FATAL_ERROR "stratary_add_extension: no extension at '${dir}'")
    endif()
    get_property(_loaded GLOBAL PROPERTY ST_EXTENSION_DIRS)
    if ("${dir}" IN_LIST _loaded)
        return()
    endif()
    set_property(GLOBAL APPEND PROPERTY ST_EXTENSION_DIRS "${dir}")
    get_filename_component(_name "${dir}" NAME)
    add_subdirectory("${dir}" "${CMAKE_BINARY_DIR}/_ext/${_name}" EXCLUDE_FROM_ALL)
endfunction()

# Called by the extension's own CMakeLists.txt. Makes the INTERFACE target StExt_<NAME>, which
# carries the include path and libraries (usage requirements) plus two custom properties
# simtary_add_app reads: the source list and the runtime libraries to stage.
function(stratary_define_extension)
    cmake_parse_arguments(EXT "" "NAME;FRAMEWORK_DIR" "LIBS;RUNTIME" ${ARGN})
    if (NOT EXT_NAME)
        message(FATAL_ERROR "stratary_define_extension: NAME is required")
    endif()
    if (NOT EXT_FRAMEWORK_DIR)
        set(EXT_FRAMEWORK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Framework")
    endif()
    file(GLOB_RECURSE _sources CONFIGURE_DEPENDS
        ${EXT_FRAMEWORK_DIR}/*.cpp
        ${EXT_FRAMEWORK_DIR}/*.h
    )
    set(_t StExt_${EXT_NAME})
    add_library(${_t} INTERFACE)
    target_include_directories(${_t} INTERFACE ${EXT_FRAMEWORK_DIR})
    if (EXT_LIBS)
        target_link_libraries(${_t} INTERFACE ${EXT_LIBS})
    endif()
    set_target_properties(${_t} PROPERTIES
        ST_EXT_SOURCES "${_sources}"
        ST_EXT_RUNTIME "${EXT_RUNTIME}"
    )
    message(STATUS "Stratary: extension ${EXT_NAME} (${EXT_FRAMEWORK_DIR})")
endfunction()
