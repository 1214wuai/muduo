cmake_minimum_required(VERSION 2.8)
project(muduo)
include_directories(.)
include_directories(muduo/base)
include_directories(muduo/net)
include_directories(/usr/include)
include_directories(/usr/include/sys)
set(CMAKE_OBJECT_PATH_MAX 2000)

# 下面两行最关键
file(GLOB_RECURSE ALL_SRC_FILE *.h *.cpp *.c)
add_executable(muduo ${ALL_SRC_FILE})