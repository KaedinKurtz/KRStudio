cmake_minimum_required(VERSION 3.20)
project(RoboticsSoftware CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)

add_executable(RoboticsSoftware
    src/main.cpp
    src/Camera.cpp
    src/Shader.cpp
    src/Mesh.cpp
    src/Robot.cpp
    src/MainWindow.cpp
    src/ViewportWidget.cpp
    include/MainWindow.hpp
    include/ViewportWidget.hpp
)

target_include_directories(RoboticsSoftware PUBLIC include)

find_package(OpenGL REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(urdfdom CONFIG REQUIRED)
target_include_directories(RoboticsSoftware PUBLIC ${urdfdom_INCLUDE_DIRS})

target_link_libraries(RoboticsSoftware PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    OpenGL::GL
    glm::glm
    ${urdfdom_LIBRARIES}
)

if(WIN32)
  get_target_property(WINDEPLOYQT_EXECUTABLE Qt6::windeployqt LOCATION)
  add_custom_command(
    TARGET RoboticsSoftware POST_BUILD
    COMMAND "${WINDEPLOYQT_EXECUTABLE}"
        --dir "$<TARGET_FILE_DIR:RoboticsSoftware>"
        --pdb
        "$<TARGET_FILE:RoboticsSoftware>"
    COMMENT "Running windeployqt to deploy Qt DLLs..."
  )
endif()