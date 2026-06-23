cmake_minimum_required(VERSION 3.20)
project(pi_satellite_rotator VERSION 0.2.0 LANGUAGES CXX)

include(CTest)

add_library(rotator_core
    src/easycomm.cpp
    src/rotator.cpp
    src/gpio_motor_backend.cpp
    src/motor_backend.cpp
    src/witmotion.cpp
)
target_include_directories(rotator_core PUBLIC include)
target_compile_features(rotator_core PUBLIC cxx_std_20)
target_compile_options(rotator_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(rotator_services
    src/tcp_server.cpp
    src/web_server.cpp
)
target_include_directories(rotator_services PUBLIC include)
target_link_libraries(rotator_services PUBLIC rotator_core)
target_compile_features(rotator_services PUBLIC cxx_std_20)
target_compile_options(rotator_services PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(pi-satellite-rotator src/main.cpp)
target_link_libraries(pi-satellite-rotator PRIVATE rotator_services)
target_compile_features(pi-satellite-rotator PRIVATE cxx_std_20)
target_compile_options(pi-satellite-rotator PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(witmotion-tool src/witmotion_tool.cpp)
target_link_libraries(witmotion-tool PRIVATE rotator_core)
target_compile_features(witmotion-tool PRIVATE cxx_std_20)
target_compile_options(witmotion-tool PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(BUILD_TESTING)
    add_executable(rotator_tests tests/test_easycomm.cpp)
    target_link_libraries(rotator_tests PRIVATE rotator_core)
    add_test(NAME rotator_tests COMMAND rotator_tests)

    add_executable(witmotion_tests tests/test_witmotion.cpp)
    target_link_libraries(witmotion_tests PRIVATE rotator_core)
    add_test(NAME witmotion_tests COMMAND witmotion_tests)

    add_executable(web_tests tests/test_web.cpp)
    target_link_libraries(web_tests PRIVATE rotator_services)
    add_test(NAME web_tests COMMAND web_tests)
endif()

install(TARGETS pi-satellite-rotator witmotion-tool RUNTIME DESTINATION bin)
