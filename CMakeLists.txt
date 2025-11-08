# ONLY FOR LOCAL TESTING PURPOSES (WINDOWS)

cmake_minimum_required(VERSION 3.28)
project(SAFOR)

set(CMAKE_CXX_STANDARD 20 CACHE STRING "C++ standard to be used")
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive")

add_executable(SAFOR SAFOR.cpp)

target_include_directories(SAFOR PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
	SAFOR
	PRIVATE
	comdlg32.a
	ws2_32.a
	winmm.a
	gdi32.a
	dwmapi.a
	ole32.a
	opengl32.a
	glu32.a
	oleaut32.a
	oleaut32.a
	uuid.a
	uxtheme.a
	comctl32.a
	mincore.a
	urlmon.a
)