cmake_minimum_required(VERSION 3.18)


project(AirportEnhancement_remake_ LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(AirportEnhancement_remake_
    main.cpp
    Enlighten.cpp
    Enlighten.h
)

add_executable(AirportEnhancementCameraRealtime
    camera_realtime/main_camera.cpp
    Enlighten.cpp
    Enlighten.h
)

add_executable(AirportEnhancementCaptureReference
    camera_realtime/capture_reference.cpp
)

find_package(OpenCV REQUIRED COMPONENTS
    core
    imgproc
    imgcodecs
    highgui
    videoio
    cudaarithm
    cudafilters
    cudaimgproc
    cudawarping
)

target_include_directories(AirportEnhancement_remake_ PRIVATE
    .
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(AirportEnhancement_remake_ PRIVATE
    ${OpenCV_LIBS}
)

target_include_directories(AirportEnhancementCameraRealtime PRIVATE
    .
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(AirportEnhancementCameraRealtime PRIVATE
    ${OpenCV_LIBS}
)

target_include_directories(AirportEnhancementCaptureReference PRIVATE
    .
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(AirportEnhancementCaptureReference PRIVATE
    ${OpenCV_LIBS}
)

if(WIN32)
    set_target_properties(AirportEnhancement_remake_ PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    set_target_properties(AirportEnhancementCameraRealtime PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    set_target_properties(AirportEnhancementCaptureReference PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif()
