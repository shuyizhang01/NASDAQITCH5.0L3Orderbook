cmake_minimum_required(VERSION 3.20)

project(SIFHFT LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ZLIB REQUIRED)
find_package(absl CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_executable(SIFHFT
        main.cpp
        itch_processor.cpp
        itch_processor.h
        orderbook.cpp
        orderbook.h
        slab_allocator.h
)

target_link_libraries(SIFHFT PRIVATE
        ZLIB::ZLIB
        absl::flat_hash_map
        Threads::Threads
)

# Profiling build (RelWithDebInfo): Full speed + symbols for Linux perf
target_compile_options(SIFHFT PRIVATE
        $<$<CONFIG:RelWithDebInfo>:
        -O3
        -march=native
        -mtune=native
        -flto=auto
        -fno-omit-frame-pointer
        >
)

# Pure Release build: Maximum HFT speed, no frame pointer overhead
target_compile_options(SIFHFT PRIVATE
        $<$<CONFIG:Release>:
        -O3
        -march=native
        -mtune=native
        -flto=auto
        -fomit-frame-pointer
        -fno-rtti
        >
)

# Link-Time Optimization for both Release and RelWithDebInfo
target_link_options(SIFHFT PRIVATE
        $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-flto=auto>
)
