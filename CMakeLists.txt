cmake_minimum_required(VERSION 3.16.0)
# Release version: embedded in the app descriptor and logged at boot.
set(PROJECT_VER "1.0.4")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(inkspeed_rsvp)
