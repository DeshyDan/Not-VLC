cmake_minimum_required(VERSION 3.30)
project(VLC_Clone C)

set(CMAKE_C_STANDARD 11)

find_package(PkgConfig REQUIRED)

## Checking for FFMPEG
pkg_check_modules(FFMPEG REQUIRED
        libavcodec
        libavformat
        libavutil
        libswscale
        libswresample
)

include_directories(${FFMPEG_INCLUDE_DIRS})
link_directories(${FFMPEG_LIBRARY_DIRS})
add_definitions(${FFMPEG_CFLAGS_OTHER})

add_executable(VLC_Clone main.c)

target_link_libraries(VLC_Clone ${FFMPEG_LIBRARIES})
