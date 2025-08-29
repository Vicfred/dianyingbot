cmake_minimum_required(VERSION 3.31.5)
project(dianying)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
set(Boost_USE_MULTITHREADED ON)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost COMPONENTS system REQUIRED)
find_package(CURL REQUIRED)

find_library(TGBOT_LIBRARY
  NAMES TgBot libTgBot
  PATHS /usr/local/lib /usr/lib /usr/lib64
)

find_path(TGBOT_INCLUDE_DIR
  NAMES tgbot/tgbot.h
  PATHS /usr/local/include /usr/include
)

add_executable(dianying src/main.cpp)
target_include_directories(dianying PRIVATE ${TGBOT_INCLUDE_DIR})
target_link_libraries(dianying PRIVATE
  ${TGBOT_LIBRARY}
  Boost::system
  OpenSSL::SSL OpenSSL::Crypto
  CURL::libcurl
  Threads::Threads
)
