SET(CMAKE_CXX_FLAGS "-std=c++11 -O3")
cmake_minimum_required(VERSION 2.8)	# Проверка версии CMake. Если версия установленой программы старее указаной, произайдёт аварийный выход.
add_executable(webserver webserver.cpp)	# Создает исполняемый файл с именем final из исходника webserver.cpp

