# plugins/EOS - Epic Online Services, as a Stratary plugin (cmake/SimtaryExtension.cmake).
#
# A game opts in with simtary_add_app(... PLUGINS EOS) and configures it from its main():
#
#   st::online::EosConfig eos;  eos.productId = "...";  ...
#   st::online::EosPlatform::Get().Configure(eos);
#
# The SDK is Epic's prebuilt C SDK, unzipped (its SDK/ folder) into Stratary's deps/eos-sdk -
# Include/, Lib/, Bin/. It is not redistributable in source form, which is why it is fetched
# separately and why this is a plugin rather than part of the MIT core. A plain C API, so it
# links straight into the exceptions-off build. Absent: Framework/online compiles to a stub
# and the project's ids do nothing. See ONLINE.md.

cmake_minimum_required(VERSION 3.19)

set(SIMTARY_EOS_DIR "${SIMTARY_DEPS_DIR}/eos-sdk" CACHE PATH "Epic Online Services C SDK (its SDK/ folder)")
if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_eos_runtime "${SIMTARY_EOS_DIR}/Bin/EOSSDK-Win64-Shipping.dll")
    set(_eos_implib  "${SIMTARY_EOS_DIR}/Lib/EOSSDK-Win64-Shipping.lib")
elseif (UNIX AND NOT APPLE AND NOT ANDROID)
    set(_eos_runtime "${SIMTARY_EOS_DIR}/Bin/libEOSSDK-Linux-Shipping.so")
    set(_eos_implib  "${_eos_runtime}")
endif()

set(_eos_libs "")
if (DEFINED _eos_runtime AND EXISTS "${_eos_runtime}" AND EXISTS "${SIMTARY_EOS_DIR}/Include/eos_sdk.h")
    if (NOT TARGET SimtaryEOS)
        add_library(SimtaryEOS SHARED IMPORTED GLOBAL)
        set_target_properties(SimtaryEOS PROPERTIES
            IMPORTED_LOCATION "${_eos_runtime}"
            IMPORTED_IMPLIB   "${_eos_implib}"
            INTERFACE_INCLUDE_DIRECTORIES "${SIMTARY_EOS_DIR}/Include"
            INTERFACE_COMPILE_DEFINITIONS "SIMTARY_HAS_EOS=1"
        )
    endif()
    set(_eos_libs SimtaryEOS)
    message(STATUS "Stratary plugin EOS: SDK at ${SIMTARY_EOS_DIR}")
else()
    message(STATUS "Stratary plugin EOS: SDK not found (deps/eos-sdk) - online services compile to a stub.")
endif()

stratary_define_extension(NAME EOS
    LIBS    ${_eos_libs}
    RUNTIME ${_eos_libs}
)
