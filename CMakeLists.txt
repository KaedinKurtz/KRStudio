# Set the minimum required version of CMake
cmake_minimum_required(VERSION 3.20)

# Define the project name and language
project(RoboticsSoftware CXX)

# --- Find Required Libraries ---
find_package(glfw3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

# Add our executable from the source file
add_executable(RoboticsSoftware src/main.cpp)

# Link ALL necessary libraries to our executable
target_link_libraries(RoboticsSoftware PRIVATE
    glfw
    OpenGL::GL
    glad::glad 
    glm::glm
)

add_custom_command(TARGET RoboticsSoftware POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_directory
                   "${CMAKE_SOURCE_DIR}/shaders" # The source folder
                   "$<TARGET_FILE_DIR:RoboticsSoftware>/shaders" # The destination folder
                   COMMENT "Copying shaders to build directory")