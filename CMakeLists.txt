cmake_minimum_required(VERSION 3.20)
project(transceiver VERSION 0.1.0 LANGUAGES C CXX)

# portaudio
set(portaudio_DIR "D:\\PortAudio\\lib\\cmake\\portaudio")
find_package(portaudio)

#asio
add_library(asio INTERFACE)
target_include_directories(asio INTERFACE "D:\\asio\\include")
target_compile_definitions(asio INTERFACE _WIN32_WINNT=0x0601)

#libtransceiver
aux_source_directory(src TRANS_FILES)
add_library(libtransceiver ${TRANS_FILES})
target_include_directories(libtransceiver PUBLIC include)
target_link_libraries(libtransceiver INTERFACE asio)
target_link_libraries(libtransceiver PRIVATE portaudio_static)


add_executable(transceiver main.cpp)
target_link_libraries(transceiver libtransceiver)