cmake_minimum_required(VERSION 3.16)
project(transceiver VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

# asio
add_library(asio INTERFACE)
target_include_directories(asio INTERFACE "${CMAKE_SOURCE_DIR}/vendor/asio-1.30.2/include")

if(MSVC)
   target_compile_definitions(asio INTERFACE _WIN32_WINNT=0x0601)
endif(MSVC)

set(PA_USE_JACK OFF)
set(PA_USE_ASIO OFF)
set(FTXUI_ENABLE_INSTALL OFF)
add_subdirectory(vendor/opus-1.5.2)
add_subdirectory(vendor/portaudio-19.7.0)
add_subdirectory(vendor/FTXUI-5.0.0)

# libtransceiver
aux_source_directory(src TRANS_FILES)
add_library(transceiver STATIC ${TRANS_FILES})
target_include_directories(transceiver PUBLIC include)
target_link_libraries(transceiver PUBLIC asio)
target_link_libraries(transceiver PRIVATE portaudio_static Opus::opus)

#debug tool
aux_source_directory(tools DEBUG_FILES)
add_executable(debug_tool ${DEBUG_FILES} ${TRANS_FILES})
target_include_directories(debug_tool PUBLIC include src)
target_link_libraries(debug_tool PUBLIC asio)
target_link_libraries(debug_tool PRIVATE portaudio_static Opus::opus ftxui::dom ftxui::component ftxui::screen)

# test
# add_subdirectory(test)
# enable_testing()
# add_test(NAME WavFile COMMAND wav_test WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
# add_test(NAME ReSample COMMAND resample_test WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# main
add_executable(interphone main.cpp)
target_link_libraries(interphone transceiver)