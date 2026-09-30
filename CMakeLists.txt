cmake_minimum_required(VERSION 3.15)
project(NX-RaptureReader LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Trova SDL2
find_package(SDL2 REQUIRED)

# Sorgenti del progetto
set(SOURCES
    source/main.cpp
)

# Target eseguibile
add_executable(${PROJECT_NAME} ${SOURCES})

# Include directory
target_include_directories(${PROJECT_NAME} PRIVATE
    include
    source
    ${SDL2_INCLUDE_DIRS}
)

# Link librerie
if(TARGET SDL2::SDL2)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE ${SDL2_LIBRARIES})
endif()

# Se stiamo effettuando la cross-compilazione per Switch con devkitPro
if(SWITCH)
    # devkitPro CMake helper per creare il file .nro
    dkp_add_nro_target(${PROJECT_NAME}
        NAME "NX-RaptureReader"
        AUTHOR "xleo36"
        VERSION "0.1.0"
        ROMFS romfs
    )
endif()
