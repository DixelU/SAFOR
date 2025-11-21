# ONLY FOR LOCAL TESTING PURPOSES (WINDOWS)

cmake_minimum_required(VERSION 3.25)
project(SAFOR)

set(CMAKE_CXX_STANDARD 23)

add_executable(SAFOR SAFOR.cpp)

if (WIN32)
	 set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive")
	 set(CMAKE_CXX_FLAGS_RELEASE " -Ofast -msse3 -funroll-all-loops -fomit-frame-pointer -floop-parallelize-all")

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
		 stdc++exp.a
	 )

else ()
	 set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
	 set(CMAKE_CXX_FLAGS_RELEASE " -Ofast -funroll-all-loops -fomit-frame-pointer -floop-parallelize-all")

	 target_include_directories(SAFOR PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

	 target_link_libraries(
		 SAFOR
		 PRIVATE
		 stdc++exp
	 )
endif ()
