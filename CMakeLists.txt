cmake_minimum_required(VERSION 3.15)
project(crypto_cpp_showcase LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Users should provide CCAPI include path and any required libraries.
# Example:
#   cmake -S . -B build -DCCAPI_INCLUDE_DIR=/path/to/ccapi/include -DCCAPI_LIBS="ccapi"
# or integrate ccapi as a submodule and add_subdirectory it, then set CCAPI_INCLUDE_DIR and CCAPI_LIBS accordingly.

set(CCAPI_INCLUDE_DIR "" CACHE PATH "Path to ccapi include directory (containing ccapi_cpp)")
set(CCAPI_LIBS "" CACHE STRING "Libraries to link for ccapi (space-separated)")

if(CCAPI_INCLUDE_DIR STREQUAL "")
  message(STATUS "CCAPI_INCLUDE_DIR not set. Configure will succeed but build may fail until you provide it. See README.md.")
endif()

include(GNUInstallDirs)

function(add_showcase exe)
  add_executable(${exe} ${ARGN})
  if(NOT CCAPI_INCLUDE_DIR STREQUAL "")
    target_include_directories(${exe} PRIVATE ${CCAPI_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include)
  else()
    target_include_directories(${exe} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  endif()
  if(NOT CCAPI_LIBS STREQUAL "")
    # Split CCAPI_LIBS by space
    separate_arguments(_ccapi_libs NATIVE_COMMAND ${CCAPI_LIBS})
    target_link_libraries(${exe} PRIVATE ${_ccapi_libs})
  endif()
endfunction()

# Executables
add_showcase(liquidity_analyzer src/liquidity_analyzer.cpp)
add_showcase(options_calculator src/options_calculator.cpp)
add_showcase(arbitrage src/arbitrage.cpp)
add_showcase(trades src/trades.cpp)

# Install (optional)
install(TARGETS liquidity_analyzer options_calculator arbitrage trades
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
