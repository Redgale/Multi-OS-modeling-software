# SPDX-License-Identifier: GPL-3.0-or-later
# The desktop application: a thin Qt layer over jig_core (architecture rule 1).
# Built as a library (jigsmith_ui) plus a small executable, so UI tests can drive
# the real windows and tools.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_library(jigsmith_ui STATIC
    MainWindow.cpp MainWindow.hpp
    Viewport.cpp Viewport.hpp
    Icons.cpp Icons.hpp
    SketchEditor.cpp SketchEditor.hpp
    FeaturePanels.cpp FeaturePanels.hpp
    InspectPanels.cpp InspectPanels.hpp
    Tutorial.cpp Tutorial.hpp
    MarkingMenu.cpp MarkingMenu.hpp
    resources.qrc
)
target_include_directories(jigsmith_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(jigsmith_ui PUBLIC jig_core Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
jig_set_warnings(jigsmith_ui)

qt_add_executable(jigsmith WIN32 MACOSX_BUNDLE main.cpp)
target_link_libraries(jigsmith PRIVATE jigsmith_ui)
jig_set_warnings(jigsmith)
set_target_properties(jigsmith PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Jigsmith"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.jigsmith.Jigsmith"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

install(TARGETS jigsmith jigsmith-cli
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin)
