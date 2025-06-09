cmake_minimum_required(VERSION 3.30)
project(Not_VLC C)

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

## SDL2_ttf
find_package(SDL2_ttf REQUIRED)
if (SDL2_TTF_FOUND)
    include_directories(${SDL2_TTF_INCLUDE_DIRS})
    message("SDL2_TTF_INCLUDE_DIRS: ${SDL2_TTF_INCLUDE_DIRS}")
    message("SDL2_TTF_LIBRARIES: ${SDL2_TTF_LIBRARIES}")
else ()
    pkg_check_modules(SDL2_TTF REQUIRED SDL2_ttf)
    include_directories(${SDL2_TTF_INCLUDE_DIRS})
endif ()


add_library(microlog STATIC
        libs/microlog/microlog.c
        libs/microlog/microlog.h)

add_library(decoders STATIC
        utils/packet_queue.h
        utils/packet_queue.c
        audio/audio.h
        audio/audio.c
        player/player.h
        player/player.c
        video/video.h
        video/video.c
        utils/sync.h
        utils/sync.c)

add_executable(Not_VLC main.c)

target_link_libraries(Not_VLC PRIVATE
        ${FFMPEG_LIBRARIES}
        ${SDL2_LIBRARIES}
        /opt/homebrew/lib/libSDL2_ttf.dylib
        /opt/homebrew/lib/libfreetype.dylib
        /opt/homebrew/lib/libharfbuzz.dylib
        microlog
        decoders)