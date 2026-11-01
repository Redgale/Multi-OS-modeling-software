# SPDX-License-Identifier: GPL-3.0-or-later
# Third-party dependencies. Every entry here must also appear in
# tools/dependencies.json (checked by tools/license_audit.py in CI).
include(FetchContent)

# --- Open CASCADE Technology (LGPL-2.1 with exception), dynamically linked ---------
find_package(OpenCASCADE CONFIG REQUIRED)
message(STATUS "OCCT ${OpenCASCADE_VERSION} from ${OpenCASCADE_DIR}")
if(OpenCASCADE_VERSION VERSION_LESS 7.6)
    message(FATAL_ERROR "Jigsmith needs OCCT 7.6 or newer")
endif()
set(JIG_OCCT_LIBS
    TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo TKPrim
    TKBO TKBool TKFeat TKFillet TKOffset TKShHealing TKMesh TKXSBase)
if(OpenCASCADE_VERSION VERSION_GREATER_EQUAL 7.8)
    list(APPEND JIG_OCCT_LIBS TKDE TKDESTEP TKDESTL)
else()
    list(APPEND JIG_OCCT_LIBS TKSTEP TKSTEPBase TKSTEPAttr TKSTEP209 TKSTL)
endif()
add_library(jig_occt INTERFACE)
target_link_libraries(jig_occt INTERFACE ${JIG_OCCT_LIBS})
target_include_directories(jig_occt SYSTEM INTERFACE ${OpenCASCADE_INCLUDE_DIR})

# --- Eigen (MPL-2.0) and Boost.Graph (BSL-1.0), header-only, used by planegcs -----
# No version in find_package: Eigen 3.4's config only accepts its own major version,
# and Homebrew ships Eigen 5 (2026). Check the minimum by hand instead.
find_package(Eigen3 REQUIRED NO_MODULE)
if(Eigen3_VERSION VERSION_LESS 3.3)
    message(FATAL_ERROR "Jigsmith needs Eigen 3.3 or newer (found ${Eigen3_VERSION})")
endif()
# Only header-only Boost.Graph is needed. Some distributions (conda-forge's
# libboost-headers) ship the headers without a CMake package, so fall back to them.
find_package(Boost 1.70 QUIET CONFIG COMPONENTS headers)
if(NOT Boost_FOUND)
    find_path(JIG_BOOST_INCLUDE boost/graph/adjacency_list.hpp REQUIRED)
    add_library(Boost::headers INTERFACE IMPORTED)
    set_target_properties(Boost::headers PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${JIG_BOOST_INCLUDE}")
    message(STATUS "Boost headers (no CMake package) at ${JIG_BOOST_INCLUDE}")
endif()

# --- FreeType (FTL, BSD-style), glyph outlines for sketch text (D-014) ---------------
find_package(Freetype 2.8 REQUIRED)

# --- nlohmann/json (MIT) -----------------------------------------------------------
find_package(nlohmann_json 3.10 QUIET CONFIG)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# --- miniz (MIT), ZIP container for .jig files -------------------------------------
FetchContent_Declare(miniz
    URL https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
    URL_HASH SHA256=ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5)
FetchContent_MakeAvailable(miniz)  # no CMakeLists upstream: this only downloads
add_library(jig_miniz STATIC ${miniz_SOURCE_DIR}/miniz.c)
target_include_directories(jig_miniz SYSTEM PUBLIC ${miniz_SOURCE_DIR})
set_target_properties(jig_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON LINKER_LANGUAGE C)

# --- GoogleTest (BSD-3-Clause), tests only -----------------------------------------
if(JIG_BUILD_TESTS)
    find_package(GTest 1.10 QUIET CONFIG)
    if(NOT GTest_FOUND)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(googletest
            URL https://github.com/google/googletest/releases/download/v1.15.2/googletest-1.15.2.tar.gz
            URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926)
        FetchContent_MakeAvailable(googletest)
    endif()
endif()

# --- Qt 6 (LGPL-3.0 modules only: Core, Gui, Widgets, OpenGL, OpenGLWidgets) -------
if(JIG_BUILD_GUI)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
    if(JIG_BUILD_TESTS)
        find_package(Qt6 6.4 REQUIRED COMPONENTS Test)  # UI tests drive the real windows
    endif()
endif()
