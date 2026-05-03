cmake_minimum_required(VERSION 3.16)
project(sk3m111_viewer VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick)

add_executable(sk3m111_viewer
    main.cpp
    serialhandler.cpp
    serialhandler.h
    resources.qrc
)

target_link_libraries(sk3m111_viewer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
)
