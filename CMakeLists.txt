cmake_minimum_required(VERSION 3.16)

project(jtty VERSION 0.1.0 LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(jtty
    src/registry.c src/callsign.c src/atom.c src/text.c src/crc.c src/fec.c
    src/modulator.c src/demodulator.c src/synchronizer.c src/receiver.c)

target_include_directories(jtty PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_link_libraries(jtty PUBLIC m)
add_executable(jtty_cli tools/main.c tools/wav.c)
set_target_properties(jtty_cli PROPERTIES OUTPUT_NAME jtty)
target_link_libraries(jtty_cli PRIVATE jtty)
add_executable(jtty_benchmark tools/benchmark.c)
target_link_libraries(jtty_benchmark PRIVATE jtty)

include(CTest)

if(BUILD_TESTING)
    foreach(name jtty reference)
        add_executable(test_${name} tests/test_${name}.c)
        target_include_directories(test_${name} PRIVATE src)
        target_link_libraries(test_${name} PRIVATE jtty)
        add_test(NAME ${name} COMMAND test_${name})
        set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    endforeach()
endif()

include(GNUInstallDirs)

install(TARGETS jtty jtty_cli
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES include/jtty.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
