# Set the minimum required version of CMake
cmake_minimum_required(VERSION 3.20)

# Define the project name and language
project(RoboticsSoftware CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Find Required Libraries ---
find_package(glfw3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(urdfdom CONFIG REQUIRED)

# Add our executable from the source file
add_executable(RoboticsSoftware
    src/main.cpp
    src/Camera.cpp
    src/Shader.cpp
)

# Link ALL necessary libraries to our executable
target_link_libraries(RoboticsSoftware PRIVATE
    glfw
    OpenGL::GL
    glad::glad 
    glm::glm
    ${urdfdom_LIBRARIES}
)

target_include_directories(RoboticsSoftware PUBLIC include)
target_include_directories(RoboticsSoftware PUBLIC ${urdfdom_INCLUDE_DIRS})

add_custom_command(TARGET RoboticsSoftware POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_directory
                   "${CMAKE_SOURCE_DIR}/shaders" # The source folder
                   "$<TARGET_FILE_DIR:RoboticsSoftware>/shaders" # The destination folder
                   COMMENT "Copying shaders to build directory")

# --- Copy the URDF file to the build directory ---
# This ensures the executable can find it when it runs.
add_custom_command(TARGET RoboticsSoftware POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy
                   "${CMAKE_SOURCE_DIR}/simple_arm.urdf"      # Source file
                   "$<TARGET_FILE_DIR:RoboticsSoftware>"       # Destination folder
                   COMMENT "Copying URDF to build directory")