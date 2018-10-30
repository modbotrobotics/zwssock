# Copyright 2018 Modbot Inc.
set(project_name zwssock)
set(library_name ${project_name})

project(${project_name})
cmake_minimum_required(VERSION 3.0.0)

# Load version
file(STRINGS version.txt VERSION_STRING)
string(REGEX MATCH ^[0-9]+ VERSION_MAJOR ${VERSION_STRING})
message([STATUS] " ${project_name} version ${VERSION_STRING}")

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

find_package(ZLIB REQUIRED)

include_directories(src)
file(GLOB SOURCES "src/zwssock/*.c")

# Library
add_library(${library_name} SHARED ${SOURCES})
target_link_libraries(${library_name} ${CONAN_LIBS} ${ZLIB_LIBRARIES})
set_target_properties(${library_name} PROPERTIES VERSION ${VERSION_STRING} SOVERSION ${VERSION_MAJOR})

# Test app
add_executable(c_test test/c_test.c)
target_link_libraries(c_test ${library_name})

install(
  TARGETS ${library_name}
  RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
  LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
  ARCHIVE DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
)

install(
  DIRECTORY src/
  DESTINATION ${CMAKE_INSTALL_PREFIX}/include/
  FILES_MATCHING PATTERN "*.h"
)