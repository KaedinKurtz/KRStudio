cmake_minimum_required(VERSION 3.20)
project(RoboticsSoftware CXX)

set(CMAKE_AUTOMOC ON) # This should trigger MOC for Q_OBJECT classes
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets)

add_executable(RoboticsSoftware
    src/main.cpp
    src/Camera.cpp
    src/Shader.cpp
    src/Mesh.cpp
    src/Robot.cpp
    src/MainWindow.cpp         # Needs MainWindow.hpp to include Q_OBJECT
    src/ViewportWidget.cpp     # Needs ViewportWidget.hpp to include Q_OBJECT
    src/DiagnosticsPanel.cpp   # Needs DiagnosticsPanel.hpp to include Q_OBJECT
    src/StaticToolbar.cpp
    include/MainWindow.hpp
    include/ViewportWidget.hpp
    include/DiagnosticsPanel.hpp
    include/StaticToolbar.hpp
    src/static_toolbar.ui
    resources.qrc
)

# --- Include Directories ---
target_include_directories(RoboticsSoftware PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include" # For your .hpp files
    "D:/RoboticsSoftware/ads/src"         # For ADS headers
)

# --- Link Directories ---
target_link_directories(RoboticsSoftware PRIVATE
    "D:/RoboticsSoftware/ads/build/Desktop_Qt_6_9_0_MSVC2022_64bit-Debug/x64/lib"
)

# --- External Libraries ---
find_package(OpenGL REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(urdfdom CONFIG REQUIRED)

target_include_directories(RoboticsSoftware PUBLIC
    ${urdfdom_INCLUDE_DIRS}
)

qt_add_resources(RoboticsSoftware "RoboticsSoftwareResources"
    resources.qrc
)

target_link_libraries(RoboticsSoftware PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGLWidgets
    OpenGL::GL
    glm::glm
    ${urdfdom_LIBRARIES}
    $<$<CONFIG:Debug>:qtadvanceddocking-qt6d>
    $<$<NOT:$<CONFIG:Debug>>:qtadvanceddocking-qt6>
)

# --- Post-build commands and Finalization ---
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

set(ADS_DLL_DIR "D:/RoboticsSoftware/ads/build/Desktop_Qt_6_9_0_MSVC2022_64bit-Debug/x64/bin") # Assuming it's in a 'bin' dir
set(ADS_DLL_DEBUG_NAME "qtadvanceddocking-qt6d.dll")
set(ADS_DLL_RELEASE_NAME "qtadvanceddocking-qt6.dll") # Assuming release name

add_custom_command(TARGET RoboticsSoftware POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ADS_DLL_DIR}/$<IF:$<CONFIG:Debug>,${ADS_DLL_DEBUG_NAME},${ADS_DLL_RELEASE_NAME}>"
        "$<TARGET_FILE_DIR:RoboticsSoftware>"
    COMMENT "Copying Qt Advanced Docking System DLL..."
)


add_custom_command(TARGET RoboticsSoftware POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:RoboticsSoftware>/shaders"
    COMMENT "Copying shaders to output directory..."
)

add_custom_command(TARGET RoboticsSoftware POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/simple_arm.urdf"
        "$<TARGET_FILE_DIR:RoboticsSoftware>/simple_arm.urdf"
    COMMENT "Copying simple_arm.urdf to output directory..."
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(RoboticsSoftware)
endif()