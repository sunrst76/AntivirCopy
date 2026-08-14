cmake_minimum_required(VERSION 3.19)
project(AntivirCopy LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)

qt_standard_project_setup()

qt_add_executable(AntivirCopy
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
)

target_link_libraries(AntivirCopy
    PRIVATE
        Qt::Core
        Qt::Widgets
)

install(TARGETS AntivirCopy
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET AntivirCopy
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

# Автоматическое копирование Qt DLL для Windows
if(WIN32)
    # Находим путь к утилите windeployqt
    get_target_property(_qmake_executable Qt6::qmake LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")

    # Запускаем деплой сразу после успешной сборки
    add_custom_command(TARGET AntivirCopy POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" "$<TARGET_FILE:AntivirCopy>"
        COMMENT "Running windeployqt..."
    )
endif()
