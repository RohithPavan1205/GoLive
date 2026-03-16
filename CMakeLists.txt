cmake_minimum_required(VERSION 3.16)

project(JaiHanuman LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets UiTools Multimedia MultimediaWidgets Concurrent)

include_directories(include)

set(HEADERS
    include/MainWindow.h
    include/CameraManager.h
    include/CameraSelectionDialog.h
    include/MediaSelectionDialog.h
    include/EffectsManager.h
    include/NativeCamera.h
    include/RecordingSettingsDialog.h
    include/StreamingSettingsDialog.h
    include/TextOverlaySettingsDialog.h
)

set(SOURCES
    main.cpp
    src/MainWindow.cpp
    src/CameraManager.cpp
    src/CameraSelectionDialog.cpp
    src/MediaSelectionDialog.cpp
    src/EffectsManager.cpp
    src/NativeCamera.mm
    resources.qrc
)

add_executable(JaiHanuman MACOSX_BUNDLE ${SOURCES} ${HEADERS})

set_target_properties(JaiHanuman PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/Info.plist
    MACOSX_BUNDLE true
)

target_link_libraries(JaiHanuman PRIVATE 
    Qt6::Widgets 
    Qt6::UiTools 
    Qt6::Multimedia 
    Qt6::MultimediaWidgets
    Qt6::Concurrent
)

if(APPLE)
    target_link_libraries(JaiHanuman PRIVATE 
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework QuartzCore"
        "-framework Foundation"
        "-framework AppKit"
    )
    
    add_custom_command(TARGET JaiHanuman POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_SOURCE_DIR}/mainwindow.ui
            $<TARGET_FILE_DIR:JaiHanuman>/../Resources/mainwindow.ui
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/effects
            $<TARGET_FILE_DIR:JaiHanuman>/../Resources/effects
    )
else()
    add_custom_command(TARGET JaiHanuman POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_SOURCE_DIR}/mainwindow.ui
            $<TARGET_FILE_DIR:JaiHanuman>/mainwindow.ui
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/effects
            $<TARGET_FILE_DIR:JaiHanuman>/effects
    )
endif()
