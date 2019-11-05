cmake_minimum_required(VERSION 3.2.2)
project(webtracereplay)
add_subdirectory(trafficserver-8.0.3)

#message(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(client
        client/client.cpp)

add_custom_target(_client
        COMMAND make debug
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/client)

add_executable(origin
        origin/origin.cpp)

add_custom_target(_origin
        COMMAND make debug
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/origin)
