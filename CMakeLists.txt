cmake_minimum_required(VERSION 2.8)
project(muduo)
# 本地
include_directories(.)
include_directories(muduo/base)
include_directories(muduo/net)

# 远程
include_directories(/usr/include)
include_directories(/usr/include/sys)
include_directories(/usr/include/c++)
include_directories(/usr/include/linux)
include_directories(/usr/include/net)
include_directories(/usr/include/arpa)
include_directories(/usr/local/include)
set(CMAKE_OBJECT_PATH_MAX 2000)

# 下面两行最关键 GLOB_RECURSE 全局递归
file(GLOB_RECURSE ALL_SRC_FILE *.h *.cpp *.c *.cc)
add_executable(muduo ${ALL_SRC_FILE})