cmake_minimum_required(VERSION 3.10)

project(lz4_stream
	DESCRIPTION "LZ4 CXX stream."
	VERSION 2.0
	LANGUAGES CXX
)

add_library(lz4_stream INTERFACE)
target_include_directories(lz4_stream INTERFACE include/)
target_link_libraries(lz4_stream INTERFACE lz4)
