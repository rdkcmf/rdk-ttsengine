################################################################################
# If not stated otherwise in this file or this component's Licenses.txt file the
# following copyright and licenses apply:
#
# Copyright 2019 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################
cmake_minimum_required(VERSION 2.8)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(LIBSOUP REQUIRED libsoup-2.4)
pkg_check_modules(ZLIB REQUIRED zlib)
pkg_check_modules(OPENSSL REQUIRED openssl)
pkg_check_modules(CURL REQUIRED libcurl)

if(ENABLE_BREAKPAD)
  add_definitions(-DUSE_BREAKPAD)
  list(APPEND LIBS "-lbreakpadwrapper")
endif()

if(USE_PXCORE_STATIC_LIBS)
  list(APPEND RT_LIBS "-lrtRemote_s -lrtCore_s")
else()
  list(APPEND RT_LIBS "-lrtRemote -lrtCore")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1y -Wall -Wextra -Werror -Wno-attributes -g -fpermissive -DRT_PLATFORM_LINUX -fPIC")

include_directories(./common/)

add_subdirectory(ttsengine)
add_subdirectory(ttsclient)
add_subdirectory(test)
