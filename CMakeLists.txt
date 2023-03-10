cmake_minimum_required(VERSION 3.24)
project(jparser)

add_executable(jparser)

target_sources(jparser PRIVATE "jparser.cpp")
set_target_properties(jparser PROPERTIES CXX_STANDARD 17)
