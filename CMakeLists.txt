cmake_minimum_required(VERSION 3.12)

project(XinputEmu)

add_executable(${PROJECT_NAME}
	src/audio.hpp src/audio.cpp
	src/client.hpp src/client.cpp
	src/host.hpp src/host.cpp
	src/server.hpp src/server.cpp
	src/mem.hpp src/mem.cpp
	src/types.hpp src/incl.hpp
	src/wx_impl.hpp src/wx_impl.cpp
	src/main.cpp)

target_include_directories(${PROJECT_NAME} PUBLIC
	${CMAKE_SOURCE_DIR}/dependencies/wxWidgets/include
	${CMAKE_SOURCE_DIR}/dependencies/wxWidgets/include/msvc
	${CMAKE_SOURCE_DIR}/dependencies/stbi
	${CMAKE_SOURCE_DIR}/dependencies/jsl
	${CMAKE_SOURCE_DIR}/dependencies/vigem/include
	${CMAKE_SOURCE_DIR}/src)

target_compile_definitions(${PROJECT_NAME} PUBLIC UNICODE _UNICODE)


target_link_libraries(${PROJECT_NAME}
	${CMAKE_SOURCE_DIR}/dependencies/JSL/x64/JoyShockLibrary.lib
	${CMAKE_SOURCE_DIR}/dependencies/vigem/lib/$(Configuration)/x64/ViGEmClient.lib
	setupapi.lib
	ws2_32.lib
	xinput.lib)

target_link_directories(${PROJECT_NAME} PUBLIC "${CMAKE_SOURCE_DIR}/dependencies/wxWidgets/lib/vc_x64_lib")

file(COPY ${CMAKE_SOURCE_DIR}/dependencies/JSL/x64/JoyShockLibrary.dll
	DESTINATION ${CMAKE_BINARY_DIR}/Debug)

file(COPY ${CMAKE_SOURCE_DIR}/dependencies/JSL/x64/JoyShockLibrary.dll
	DESTINATION ${CMAKE_BINARY_DIR}/Release)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
