cmake_minimum_required(VERSION 3.0...3.25)
project(murmurhash VERSION 0.1.0 LANGUAGES CXX C)

add_library(${PROJECT_NAME} MurmurHash3.cpp)


