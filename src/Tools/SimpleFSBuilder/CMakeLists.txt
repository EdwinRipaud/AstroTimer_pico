cmake_minimum_required(VERSION 3.13)
# set static environment variables
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
# set project name
set(PROGRAM_NAME SimpleFSBuilder)
project(${PROGRAM_NAME} C CXX ASM)

add_executable(${PROGRAM_NAME} SimpleFSBuilder.cpp)
#set_property(TARGET SimpleFSBuilder PROPERTY CXX_STANDARD 17)
