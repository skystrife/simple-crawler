cmake_minimum_required(VERSION 3.2.0)
project(simple-crawler)

option(ENABLE_LIBCXX "Use libc++ for the C++ standard library (only for clang)" ON)
option(ENABLE_JEMALLOC "Link against jemalloc if available" ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CMakePushCheckState)
include(ExternalProject)
include(deps/meta-cmake/SetClangOptions.cmake)
include(deps/meta-cmake/CompilerKludges.cmake)

find_package(LibXml2 REQUIRED)

cmake_push_check_state()

# Work around CMake not propagating the standard flag down to the compiler
set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} ${CMAKE_CXX14_STANDARD_COMPILE_OPTION}")

# Check if there is no build type set. If meta itself is the root project,
# compile it in release mode instead. If we aren't the root project, just
# continue along with whatever we would do ordinarily (they *really* should
# be specifying a build type, but...)
if (NOT CMAKE_BUILD_TYPE AND CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  message("-- No build type selected, defaulting to Release")
  set(CMAKE_BUILD_TYPE "Release")
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/deps/meta-cmake/)

add_library(sc-definitions INTERFACE)
target_include_directories(sc-definitions INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(UNIX OR MINGW)
  target_compile_options(sc-definitions INTERFACE -Wall -Wextra -pedantic)

  if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    SetClangOptions(sc-definitions)
  endif()
endif()

if (LIBDL_LIBRARY)
  target_link_libraries(sc-definitions INTERFACE ${LIBDL_LIBRARY})
endif()

if (CXXABI_LIBRARY)
  target_link_libraries(sc-definitions INTERFACE ${CXXABI_LIBRARY})
endif()

if (LIBCXX_FOUND)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
  target_include_directories(sc-definitions SYSTEM INTERFACE ${LIBCXX_INCLUDE_DIR})
  target_compile_options(sc-definitions INTERFACE ${LIBCXX_OPTIONS})
  target_link_libraries(sc-definitions INTERFACE -L${LIBCXX_LIB_PATH})
  target_link_libraries(sc-definitions INTERFACE ${LIBCXX_LIBRARY})
endif()

find_library(JEMALLOC_LIB NAMES jemalloc)
if (JEMALLOC_LIB AND ENABLE_JEMALLOC)
  message("-- Using jemalloc: ${JEMALLOC_LIB}")
  target_link_libraries(sc-definitions INTERFACE ${JEMALLOC_LIB})
else()
  message("-- Using regular malloc; consider installing jemalloc")
endif()

# set a bunch of preprocessor variables to work around various compiler and
# standard library bugs
CompilerKludges()

target_link_libraries(sc-definitions INTERFACE compiler-kludges)
cmake_pop_check_state()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

ExternalProject_Add(gumbo-parser-ep
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/gumbo-parser
  PREFIX deps/gumbo-parser
  BUILD_IN_SOURCE 1
  CONFIGURE_COMMAND ./autogen.sh
  COMMAND ./configure --prefix=<INSTALL_DIR>
  BUILD_COMMAND ${MAKE}
  BUILD_BYPRODUCTS <INSTALL_DIR>/lib/${CMAKE_FIND_LIBRARY_PREFIXES}gumbo.a
  INSTALL_COMMAND make install)

ExternalProject_Get_Property(gumbo-parser-ep INSTALL_DIR)
set(GUMBO_INCLUDE_DIR ${INSTALL_DIR}/include)
set(GUMBO_LIB_DIR ${INSTALL_DIR}/lib)

add_library(gumbo-parser STATIC IMPORTED)
set_target_properties(gumbo-parser PROPERTIES
  IMPORTED_LOCATION ${GUMBO_LIB_DIR}/${CMAKE_FIND_LIBRARY_PREFIXES}gumbo.a)
add_dependencies(gumbo-parser gumbo-parser-ep)

ExternalProject_Add(gumbo-libxml-ep
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/gumbo-libxml
  PREFIX deps/gumbo-libxml
  BUILD_IN_SOURCE 1
  CONFIGURE_COMMAND ./autogen.sh
  COMMAND ${CMAKE_COMMAND} -E env GUMBO_CFLAGS="-I${GUMBO_INCLUDE_DIR}" GUMBO_LIBS="-L${GUMBO_LIB_DIR} -lgumbo" ./configure --prefix=<INSTALL_DIR>
  BUILD_COMMAND ${MAKE}
  BUILD_BYPRODUCTS <INSTALL_DIR>/lib/${CMAKE_FIND_LIBRARY_PREFIXES}gumbo_xml.a
  INSTALL_COMMAND make install)

ExternalProject_Get_Property(gumbo-libxml-ep INSTALL_DIR)
set(GUMBO_LIBXML_INCLUDE_DIR ${INSTALL_DIR}/include)
set(GUMBO_LIBXML_LIB_DIR ${INSTALL_DIR}/lib)

add_library(gumbo-libxml STATIC IMPORTED)
set_target_properties(gumbo-libxml PROPERTIES
  IMPORTED_LOCATION ${GUMBO_LIBXML_LIB_DIR}/${CMAKE_FIND_LIBRARY_PREFIXES}gumbo_xml.a)
add_dependencies(gumbo-libxml gumbo-libxml-ep)

add_subdirectory(deps/cpr EXCLUDE_FROM_ALL)
add_subdirectory(deps/cpptoml EXCLUDE_FROM_ALL)
add_subdirectory(src)
