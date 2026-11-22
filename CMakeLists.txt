cmake_minimum_required(VERSION 3.15)
project(NX-RaptureReader LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    source/main.cpp
    source/app_controller.cpp
    source/cbz.cpp
    source/epub.cpp
    source/library.cpp
    source/pdf.cpp
    source/progress.cpp
    source/reader.cpp
    source/settings.cpp
    source/sleep_guard.cpp
    source/text_renderer.cpp
    source/touch.cpp
    source/ui.cpp
    source/zip_reader.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)

target_include_directories(${PROJECT_NAME} PRIVATE
    include
    source
)

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
    third_party
)

# When cross-compiling for Nintendo Switch with devkitPro
if(NINTENDO_SWITCH OR SWITCH)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(FREETYPE REQUIRED freetype2)
    pkg_check_modules(ZLIB REQUIRED zlib)
    pkg_check_modules(WEBP REQUIRED libwebp)

    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_MUPDF)

    target_include_directories(${PROJECT_NAME} PRIVATE
        ${SDL2_STATIC_INCLUDE_DIRS}
        ${FREETYPE_STATIC_INCLUDE_DIRS}
        ${ZLIB_STATIC_INCLUDE_DIRS}
        ${WEBP_STATIC_INCLUDE_DIRS}
    )

    target_compile_options(${PROJECT_NAME} PRIVATE
        ${SDL2_STATIC_CFLAGS_OTHER}
        ${FREETYPE_STATIC_CFLAGS_OTHER}
        ${ZLIB_STATIC_CFLAGS_OTHER}
        ${WEBP_STATIC_CFLAGS_OTHER}
    )

    target_link_directories(${PROJECT_NAME} PRIVATE
        ${SDL2_STATIC_LIBRARY_DIRS}
        ${FREETYPE_STATIC_LIBRARY_DIRS}
        ${ZLIB_STATIC_LIBRARY_DIRS}
        ${WEBP_STATIC_LIBRARY_DIRS}
    )

    target_link_options(${PROJECT_NAME} PRIVATE
        -Wl,--gc-sections
        ${SDL2_STATIC_LDFLAGS_OTHER}
        ${FREETYPE_STATIC_LDFLAGS_OTHER}
        ${ZLIB_STATIC_LDFLAGS_OTHER}
        ${WEBP_STATIC_LDFLAGS_OTHER}
    )

    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${SDL2_STATIC_LIBRARIES}
        mupdf
        mupdf-third
        jpeg
        ${FREETYPE_STATIC_LIBRARIES}
        ${FREETYPE_STATIC_LIBRARIES}
        ${WEBP_STATIC_LIBRARIES}
        ${ZLIB_STATIC_LIBRARIES}
    )

    nx_generate_nacp("${CMAKE_CURRENT_BINARY_DIR}/NX-RaptureReader.nacp"
        NAME "NX-RaptureReader"
        AUTHOR "xleo36"
        VERSION "0.1.0"
    )

    nx_create_nro(${PROJECT_NAME}
        NACP "${CMAKE_CURRENT_BINARY_DIR}/NX-RaptureReader.nacp"
        ROMFS "${CMAKE_CURRENT_SOURCE_DIR}/romfs"
    )
else()
    find_package(SDL2 REQUIRED)
    find_package(Freetype REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WEBP REQUIRED libwebp)

    target_include_directories(${PROJECT_NAME} PRIVATE
        ${SDL2_INCLUDE_DIRS}
        ${WEBP_INCLUDE_DIRS}
    )

    target_link_directories(${PROJECT_NAME} PRIVATE
        ${WEBP_LIBRARY_DIRS}
    )

    if(TARGET SDL2::SDL2)
        target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2)
    else()
        target_link_libraries(${PROJECT_NAME} PRIVATE ${SDL2_LIBRARIES})
    endif()

    target_link_libraries(${PROJECT_NAME} PRIVATE
        Freetype::Freetype
        ZLIB::ZLIB
        ${WEBP_LIBRARIES}
    )

    if(APPLE)
        target_link_libraries(${PROJECT_NAME} PRIVATE
            "-framework CoreGraphics"
            "-framework CoreFoundation"
        )
    endif()
endif()
