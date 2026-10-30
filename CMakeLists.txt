cmake_minimum_required(VERSION 3.16)
project(ms900-dpi C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Qt 6 preferred, Qt 5.15 also works
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

add_executable(ms900-dpi-gui src/main.cpp src/ms900.c)
target_include_directories(ms900-dpi-gui PRIVATE src)
target_link_libraries(ms900-dpi-gui PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

install(TARGETS ms900-dpi-gui RUNTIME DESTINATION bin)
install(FILES 70-ms900-dpi.rules DESTINATION /etc/udev/rules.d)
install(FILES ms900-dpi.desktop DESTINATION share/applications)
