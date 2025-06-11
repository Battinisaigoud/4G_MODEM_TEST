# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)
# Get clean Git tag version
execute_process(
    COMMAND git describe --tags --abbrev=0
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(GIT_TAG)
    message("Using git tag as project version: ${GIT_TAG}")
    set(PROJECT_VER "${GIT_TAG}" CACHE INTERNAL "Project version from Git")
    set(CONFIG_APP_PROJECT_VER "${GIT_TAG}" CACHE INTERNAL "Force version in app desc")
else()
    message("Git tag not found. Using git commit hash as project version.")
    add_definitions(-DPROJECT_VER="${PROJECT_VER}")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(4G_modem_Test)
