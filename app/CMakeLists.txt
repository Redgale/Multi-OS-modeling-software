# SPDX-License-Identifier: GPL-3.0-or-later
# The desktop application: a thin Qt layer over jig_core (architecture rule 1).
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

qt_add_executable(jigsmith WIN32 MACOSX_BUNDLE
    main.cpp
    MainWindow.cpp MainWindow.hpp
    Viewport.cpp Viewport.hpp
    resources.qrc
)
target_link_libraries(jigsmith PRIVATE jig_core Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
jig_set_warnings(jigsmith)
set_target_properties(jigsmith PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Jigsmith"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.jigsmith.Jigsmith"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

install(TARGETS jigsmith jigsmith-cli
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin)
