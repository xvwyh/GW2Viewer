cmake_minimum_required(VERSION 3.10)
project(libgw2dattools)

# Set a default build type if none was specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message("Setting build type to 'RelWithDebInfo' as none was specified.")
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release"
    "MinSizeRel" "RelWithDebInfo")
endif()

# Set options
option(BUILD_EXAMPLES "Build the examples." OFF)

# libgw2dattools

set(LIBGW2DATTOOLS_VERSION_MAJOR 1)
set(LIBGW2DATTOOLS_VERSION_MINOR 0)
set(LIBGW2DATTOOLS_VERSION_PATCH 0)
set(LIBGW2DATTOOLS_VERSION "${LIBGW2DATTOOLS_VERSION_MAJOR}.${LIBGW2DATTOOLS_VERSION_MINOR}.${LIBGW2DATTOOLS_VERSION_PATCH}")

set(LIBGW2DATTOOLS_SOURCE_DIR ${PROJECT_SOURCE_DIR}/src)
set(LIBGW2DATTOOLS_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)

set(LIBGW2DATTOOLS_SOURCE_FILES
    ${LIBGW2DATTOOLS_SOURCE_DIR}/gw2dattools/c_api/compression_inflateDatFileBuffer.cpp
    ${LIBGW2DATTOOLS_SOURCE_DIR}/gw2dattools/compression/huffmanTreeUtils.cpp
    ${LIBGW2DATTOOLS_SOURCE_DIR}/gw2dattools/compression/inflateDatFileBuffer.cpp
    ${LIBGW2DATTOOLS_SOURCE_DIR}/gw2dattools/compression/inflateTextureFileBuffer.cpp
    ${LIBGW2DATTOOLS_SOURCE_DIR}/gw2dattools/exception/Exception.cpp
    ${LIBGW2DATTOOLS_SOURCE_DIR}/gw2dattools/format/ANDat.cpp
    ${LIBGW2DATTOOLS_SOURCE_DIR}/gw2dattools/format/Mapping.cpp
    ${LIBGW2DATTOOLS_SOURCE_DIR}/gw2dattools/format/Mft.cpp
    ${LIBGW2DATTOOLS_SOURCE_DIR}/gw2dattools/interface/ANDatInterface.cpp
)

set(LIBGW2DATTOOLS_HEADER_FILES
    ${LIBGW2DATTOOLS_INCLUDE_DIR}/gw2dattools/c_api/compression_inflateDatFileBuffer.h
    ${LIBGW2DATTOOLS_INCLUDE_DIR}/gw2dattools/compression/inflateDatFileBuffer.h
    ${LIBGW2DATTOOLS_INCLUDE_DIR}/gw2dattools/compression/inflateTextureFileBuffer.h
    ${LIBGW2DATTOOLS_INCLUDE_DIR}/gw2dattools/exception/Exception.h
    ${LIBGW2DATTOOLS_INCLUDE_DIR}/gw2dattools/interface/ANDatInterface.h
)

source_group(include FILES ${LIBGW2DATTOOLS_HEADER_FILES})

add_library(gw2dattools SHARED ${LIBGW2DATTOOLS_SOURCE_FILES} ${LIBGW2DATTOOLS_HEADER_FILES})

target_compile_definitions(gw2dattools PRIVATE LIBGW2DATTOOLS_EXPORT)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR
    "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    target_compile_options(gw2dattools PRIVATE -Wall)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    target_compile_options(gw2dattools PRIVATE /W4)
endif()

target_include_directories(gw2dattools PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(gw2dattools PUBLIC $<INSTALL_INTERFACE:include> $<BUILD_INTERFACE:${LIBGW2DATTOOLS_INCLUDE_DIR}>)

set_property(TARGET gw2dattools PROPERTY VERSION ${LIBGW2DATTOOLS_VERSION})
set_property(TARGET gw2dattools PROPERTY SOVERSION ${LIBGW2DATTOOLS_VERSION_MAJOR})
set_property(TARGET gw2dattools PROPERTY DEBUG_POSTFIX d)

# samples
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# installation - spefify files to package
install(TARGETS gw2dattools EXPORT libgw2dattoolsTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PRIVATE_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${LIBGW2DATTOOLS_INCLUDE_DIR}/ DESTINATION include FILES_MATCHING PATTERN "*.h")

if(MSVC)
    install(FILES $<TARGET_FILE_DIR:gw2dattools>/gw2dattoolsd.pdb DESTINATION bin CONFIGURATIONS Debug)
endif()

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    ${CMAKE_BINARY_DIR}/cmake/libgw2dattoolsVersion.cmake
    VERSION ${LIBGW2DATTOOLS_VERSION}
    COMPATIBILITY SameMajorVersion
)

# installation - build tree specific package config files
export(EXPORT libgw2dattoolsTargets FILE ${CMAKE_BINARY_DIR}/libgw2dattoolsTargets.cmake)
configure_file(${PROJECT_SOURCE_DIR}/libgw2dattoolsConfig.cmake.in
    ${CMAKE_BINARY_DIR}/libgw2dattoolsConfig.cmake
    COPYONLY
)

# installation - relocatable package config files
configure_package_config_file(${PROJECT_SOURCE_DIR}/libgw2dattoolsConfig.cmake.in
                              ${CMAKE_CURRENT_BINARY_DIR}/cmake/libgw2dattoolsConfig.cmake
                              INSTALL_DESTINATION cmake
)
install(EXPORT libgw2dattoolsTargets
    FILE libgw2dattoolsTargets.cmake
    DESTINATION "lib/cmake/libgw2dattools"
)
install(FILES
    ${CMAKE_BINARY_DIR}/cmake/libgw2dattoolsConfig.cmake
    ${CMAKE_BINARY_DIR}/cmake/libgw2dattoolsVersion.cmake
    DESTINATION "lib/cmake/libgw2dattools"
)

if(NOT TARGET uninstall)
  configure_file(
      ${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in
      ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
      IMMEDIATE @ONLY)

  add_custom_target(uninstall
      COMMAND ${CMAKE_COMMAND} -P
      ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()
