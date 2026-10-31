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
    Translations.cpp Translations.hpp
    resources.qrc
)
target_include_directories(jigsmith_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(jigsmith_ui PUBLIC jig_core Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
jig_set_warnings(jigsmith_ui)

# Interface translations (optional: built when Qt Linguist's tools are installed).
#   cmake --build build --target jigsmith_ui_lupdate   refreshes translations/jigsmith.ts
#   python3 tools/pseudo_translate.py translations/jigsmith.ts translations/jigsmith_qps.ts
find_package(Qt6 QUIET COMPONENTS LinguistTools)
if(Qt6LinguistTools_FOUND)
    file(GLOB JIG_TS_FILES ${PROJECT_SOURCE_DIR}/translations/jigsmith_*.ts)
    qt_add_lupdate(jigsmith_ui TS_FILES ${PROJECT_SOURCE_DIR}/translations/jigsmith.ts OPTIONS -no-obsolete -locations relative)
    qt_add_lrelease(jigsmith_ui TS_FILES ${JIG_TS_FILES} QM_FILES_OUTPUT_VARIABLE JIG_QM_FILES)
    qt_add_resources(jigsmith_ui "i18n" PREFIX "/i18n" BASE ${CMAKE_CURRENT_BINARY_DIR} FILES ${JIG_QM_FILES})
    message(STATUS "Jigsmith: interface translations: ${JIG_TS_FILES}")
else()
    message(STATUS "Jigsmith: Qt LinguistTools not found; the interface is built in English only")
endif()

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
