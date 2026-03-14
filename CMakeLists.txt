# ONLY FOR LOCAL TESTING PURPOSES (WINDOWS)

cmake_minimum_required(VERSION 3.25)
project(SAFOR)

set(CMAKE_CXX_STANDARD 23)

add_executable(SAFOR SAFOR.cpp)

if (WIN32)
	set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive")
	set(CMAKE_CXX_FLAGS " -Ofast -msse3 -funroll-all-loops -fomit-frame-pointer -floop-parallelize-all -Wall -Wextra")

	target_include_directories(SAFOR PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

	target_link_libraries(
			SAFOR
			PRIVATE
			comctl32.a
			stdc++exp.a
	)

else ()
	set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
	set(CMAKE_CXX_FLAGS " -Ofast -funroll-all-loops -fomit-frame-pointer -floop-parallelize-all")

	target_include_directories(SAFOR PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

	target_link_libraries(
			SAFOR
			PRIVATE
			stdc++exp
	)
endif ()
