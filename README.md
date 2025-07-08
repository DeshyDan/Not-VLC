# Video Player From Scratch
## 📺 Check out build on YouTube 
[Link to YouTube Video](https://youtu.be/ObqVTnCtLXY)
[![Watch the video](docs/thumbnail.png)](https://youtu.be/ObqVTnCtLXY)

<sub><sup>(Click thumbnail to watch)</sup></sub>

Video player built using FFmpeg for processing and SDL for rendering. It supports basic playback controls
and synchronization. I wrote in C, which i learnt for the first time when I took on this project. My C game is still a
bit wack but eyy, it works ;).

## Table of Contents

- [Table of Contents](#table-of-contents)
- [Features](#features)
- [Supported Platforms](#supported-platforms)
- [Architecture Overview](#architecture-overview)
- [Building and Running](#building-and-running)
- [Known Limitations](#known-limitations)

## Features

- **Play/Pause**: Toggle playback with spacebar or on-screen button
- **Seeking**:
    - Short jumps: ±10 seconds (left/right arrows)
    - Long jumps: ±60 seconds (up/down arrows)
- **Audio-Video Sync**: Automatic synchronization with audio as master clock
- **Basic UI**: On-screen controls for play/pause and seeking

## Supported Platforms

- macOS (primary development platform)
- *Note: I don't know if it will work on other platforms*

## Architecture Overview

The player is organized into several key modules:

### Core Modules

1. **Player** (`player.c/h`)
    - Main event loop and state management
    - Handles user input and playback controls
    - Coordinates audio/video threads

2. **Audio** (`audio.c/h`)
    - Audio stream decoding and resampling
    - SDL audio callback implementation
    - Audio clock management for synchronization

3. **Video** (`video.c/h`)
    - Video stream decoding and frame processing
    - YUV conversion and texture management
    - Frame timing and display scheduling

4. **Synchronization** (`sync.c/h`)
    - Implements audio-video synchronization logic
    - Supports multiple sync strategies (audio master, video master, external)

5. **Packet Queue** (`packet_queue.c/h`)
    - Thread-safe FIFO queue for AVPackets
    - Used for both audio and video streams

## Building and Running

### Prerequisites

- FFmpeg libraries (avcodec, avformat, avutil, swscale, swresample)
- SDL2 (with SDL_ttf for UI elements)
- C compiler (tested with clang)

### Build Instructions

1. Install dependencies:
   ```bash
   brew install ffmpeg sdl2 sdl2_ttf
   ```

2. Clone the repository:

```bash
git clone https://github.com/deshydan/not-vlc.git
cd not-vlc
```

3. Build the project:

```bash
mkdir build && cd build
cmake ..
make
```

4. Run the player:

```bash
./Not_VLC 
```


## Known Limitations
- Why is there no option to select video files? I don't know, I just didn't implement it.
- Limited video format support
- Basic UI with minimal controls. Improvement needed include -> video progress bar(very cool), volume and speed control
- Seeking can be choppy with some codecs
- No volume control or fullscreen toggle yet
- Not cross-platform compatible

Please open issues or pull requests to contribute.

