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
message("FFMPEG_INCLUDE_DIRS: ${FFMPEG_INCLUDE_DIRS}")
message("FFMPEG_LIBRARY_DIRS: ${FFMPEG_LIBRARY_DIRS}")
message("FFMPEG_LIBRARIES: ${FFMPEG_LIBRARIES}")
message("FFMPEG_CFLAGS_OTHER: ${FFMPEG_CFLAGS_OTHER}")

include_directories(${FFMPEG_INCLUDE_DIRS})
link_directories(${FFMPEG_LIBRARY_DIRS})
add_definitions(${FFMPEG_CFLAGS_OTHER})

##  SDL2
find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

add_library(microlog STATIC
        libs/microlog/microlog.c
        libs/microlog/microlog.h)

add_library(decoders STATIC
        decoders/decoder.c
        decoders/decoder.h)

add_executable(VLC_Clone main.c)

target_link_libraries(VLC_Clone PRIVATE
        ${FFMPEG_LIBRARIES}
        ${SDL2_LIBRARIES}
        microlog
        decoders)