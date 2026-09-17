cmake_minimum_required(VERSION 3.21)

project(KineticGaltonBoard
    VERSION 1.0.0
    DESCRIPTION "Maxwell-Boltzmann kinetic Galton board simulator"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)
qt_standard_project_setup()

qt_add_executable(KineticGaltonBoard WIN32
    src/main.cpp
    src/Geometry.h            src/Geometry.cpp
    src/Simulation.h          src/Simulation.cpp
    src/BoardGLView.h         src/BoardGLView.cpp
    src/DistributionView.h    src/DistributionView.cpp
    src/MainWindow.h          src/MainWindow.cpp
    assets/assets.qrc
)

if(WIN32)
    if(MINGW)
        # windres forwards include paths incorrectly when they contain spaces.
        # This resource needs no preprocessing includes; run beside the icon.
        set(icon_object "${CMAKE_CURRENT_BINARY_DIR}/app-icon.o")
        add_custom_command(OUTPUT "${icon_object}"
            COMMAND "${CMAKE_RC_COMPILER}" -O coff -i app.rc -o "${icon_object}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            DEPENDS assets/app.rc assets/app.ico
            VERBATIM)
        target_sources(KineticGaltonBoard PRIVATE "${icon_object}")
    else()
        target_sources(KineticGaltonBoard PRIVATE assets/app.rc)
        target_include_directories(KineticGaltonBoard PRIVATE assets)
    endif()
endif()

target_include_directories(KineticGaltonBoard PRIVATE src)
target_link_libraries(KineticGaltonBoard PRIVATE
    Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)

if(MSVC)
    target_compile_options(KineticGaltonBoard PRIVATE /W3 /permissive- /fp:fast)
else()
    target_compile_options(KineticGaltonBoard PRIVATE -Wall -Wextra -ffast-math)
endif()

# Keep a loose copy of the extracted outline next to the binary so re-running
# the Blender pipeline takes effect without a rebuild.
add_custom_command(TARGET KineticGaltonBoard POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:KineticGaltonBoard>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/geometry.json
            $<TARGET_FILE_DIR:KineticGaltonBoard>/assets/geometry.json
    COMMENT "Copying extracted geometry next to the binary")
