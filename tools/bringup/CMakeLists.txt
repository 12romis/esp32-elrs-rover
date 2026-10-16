cmake_minimum_required(VERSION 3.22)

# Діагностичні прошивки P0–P7: окремий проєкт, не входить у основну прошивку.
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../../components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(bringup)
