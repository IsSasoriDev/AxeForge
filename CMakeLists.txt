cmake_minimum_required(VERSION 3.16)
# The firmware version comes from version.txt (CI writes the git tag into it).
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(axeforge)
