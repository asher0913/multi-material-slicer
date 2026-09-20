cmake_minimum_required(VERSION 3.16)

project(MultiMaterialSlicer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/ui)

find_package(Qt5 5.12 REQUIRED COMPONENTS Widgets OpenGL)

add_executable(MultiMaterialSlicer
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/ModelTypes.h
    src/StlMesh.cpp
    src/StlMesh.h
    src/OpenGLView.cpp
    src/OpenGLView.h
    src/SliceExporter.cpp
    src/SliceExporter.h
    src/ConfigWriter.cpp
    src/ConfigWriter.h
    src/SliceWorker.cpp
    src/SliceWorker.h
    src/PresetLibrary.h
    src/PresetLoader.cpp
    src/PresetLoader.h
    ui/MainWindow.ui
    resources/resources.qrc
)

target_include_directories(MultiMaterialSlicer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)

set(PRESET_CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/config/machine_material_presets.yaml)
target_link_libraries(MultiMaterialSlicer PRIVATE Qt5::Widgets Qt5::OpenGL)

if(WIN32)
    set(BACKEND_TOOL ${CMAKE_CURRENT_SOURCE_DIR}/backend_dist/slice_merge_tool.exe)
    set(BACKEND_OUTPUT_NAME slice_merge_tool.exe)
    set(STEP_TOOL ${CMAKE_CURRENT_SOURCE_DIR}/backend_dist/step_to_stl_parts.exe)
    set(STEP_OUTPUT_NAME step_to_stl_parts.exe)
else()
    set(BACKEND_TOOL ${CMAKE_CURRENT_SOURCE_DIR}/backend_dist/slice_merge_tool)
    set(BACKEND_OUTPUT_NAME slice_merge_tool)
    set(STEP_TOOL ${CMAKE_CURRENT_SOURCE_DIR}/backend_dist/step_to_stl_parts)
    set(STEP_OUTPUT_NAME step_to_stl_parts)
endif()

if(APPLE)
    set_target_properties(MultiMaterialSlicer PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER com.multimaterial.slicer
        MACOSX_BUNDLE_BUNDLE_NAME MultiMaterialSlicer
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
    # Non-code resources stay in Resources. The executable backend helper lives
    # under MacOS because recent macOS releases can kill helper executables when
    # they are launched from Contents/Resources.
    add_custom_command(TARGET MultiMaterialSlicer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_BUNDLE_CONTENT_DIR:MultiMaterialSlicer>/Resources
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/slice_1080p.py
            $<TARGET_BUNDLE_CONTENT_DIR:MultiMaterialSlicer>/Resources/slice_1080p.py
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/step_to_stl_parts.py
            $<TARGET_BUNDLE_CONTENT_DIR:MultiMaterialSlicer>/Resources/step_to_stl_parts.py
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PRESET_CONFIG}
            $<TARGET_BUNDLE_CONTENT_DIR:MultiMaterialSlicer>/Resources/machine_material_presets.yaml
    )
    if(EXISTS ${BACKEND_TOOL})
        add_custom_command(TARGET MultiMaterialSlicer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${BACKEND_TOOL}
                $<TARGET_BUNDLE_CONTENT_DIR:MultiMaterialSlicer>/MacOS/slice_merge_tool
        )
    endif()
    if(EXISTS ${STEP_TOOL})
        add_custom_command(TARGET MultiMaterialSlicer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${STEP_TOOL}
                $<TARGET_BUNDLE_CONTENT_DIR:MultiMaterialSlicer>/MacOS/step_to_stl_parts
        )
    endif()
else()
    add_custom_command(TARGET MultiMaterialSlicer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/slice_1080p.py
            $<TARGET_FILE_DIR:MultiMaterialSlicer>/slice_1080p.py
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/step_to_stl_parts.py
            $<TARGET_FILE_DIR:MultiMaterialSlicer>/step_to_stl_parts.py
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PRESET_CONFIG}
            $<TARGET_FILE_DIR:MultiMaterialSlicer>/machine_material_presets.yaml
    )
    if(EXISTS ${BACKEND_TOOL})
        add_custom_command(TARGET MultiMaterialSlicer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${BACKEND_TOOL}
                $<TARGET_FILE_DIR:MultiMaterialSlicer>/${BACKEND_OUTPUT_NAME}
        )
    endif()
    if(EXISTS ${STEP_TOOL})
        add_custom_command(TARGET MultiMaterialSlicer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${STEP_TOOL}
                $<TARGET_FILE_DIR:MultiMaterialSlicer>/${STEP_OUTPUT_NAME}
        )
    endif()
endif()

if(WIN32)
    set_target_properties(MultiMaterialSlicer PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

install(TARGETS MultiMaterialSlicer
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
