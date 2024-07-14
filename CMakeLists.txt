cmake_minimum_required(VERSION 3.20)
project(transceiver VERSION 0.1.0 LANGUAGES C CXX)

# portaudio
set(portaudio_DIR "D:\\PortAudio\\lib\\cmake\\portaudio")
find_package(portaudio)

#asio
add_library(asio INTERFACE)
target_include_directories(asio INTERFACE "D:\\asio\\include")
target_compile_definitions(asio INTERFACE _WIN32_WINNT=0x0601)

add_executable(transceiver transceiver.cpp main.cpp)
target_link_libraries(transceiver portaudio_static asio)

