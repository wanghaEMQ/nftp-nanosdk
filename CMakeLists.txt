#
# This software is supplied under the terms of the MIT License, a
# copy of which should be located in the distribution where this
# file was obtained (LICENSE.txt).  A copy of the license may also be
# found online at https://opensource.org/licenses/MIT.

cmake_minimum_required (VERSION 2.8.12)

project(nftp_client)

find_package(nng CONFIG REQUIRED)

find_package(Threads)

include_directories(".")

add_executable(nftp_clie nftp_clie.c)
target_link_libraries(nftp_clie nng nftp-codec-static)
target_link_libraries(nftp_clie ${CMAKE_THREAD_LIBS_INIT})

add_executable(nftp_serv nftp_serv.c)
target_link_libraries(nftp_serv nng nftp-codec-static)
target_link_libraries(nftp_serv ${CMAKE_THREAD_LIBS_INIT})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -fsanitize=address")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fsanitize=address")

target_compile_definitions(nftp_clie PRIVATE NNG_ELIDE_DEPRECATED)
target_compile_definitions(nftp_serv PRIVATE NNG_ELIDE_DEPRECATED)
