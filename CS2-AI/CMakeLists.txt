cmake_minimum_required(VERSION 3.8)

# 工程名建议用下划线；最终产物用 OUTPUT_NAME 指定为连字符
project(sg_tools LANGUAGES CXX)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# 如你已有自定义的 windeployqt.cmake，请确保它在 CMAKE_MODULE_PATH 中
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}")
include(windeployqt)

# Qt & MSVC
if (MSVC)
    # 若已在环境变量里设置 QT_MSVC_64，如 C:/Qt/6.6.3/msvc2019_64
    if(DEFINED ENV{QT_MSVC_64})
        list(APPEND CMAKE_PREFIX_PATH $ENV{QT_MSVC_64})
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

# ========= 你的原有文件清单（保持不变）=========
set(UTILITY_HEADER
    "include/Utility/json.hpp"
    "include/Utility/Logger.h"
    "include/Utility/Logging.h"
    "include/Utility/Vec2D.h"
    "include/Utility/Vec3D.h"
    "include/Utility/Utility.h"
    "include/Utility/Dijkstra.h"
)

set(UI_HEADER
    "include/UI/CS2Runner.h"
    "include/UI/MainWindow.h"
    "include/UI/NavmeshEditor.h"
    "include/UI/NavmeshEditorWidget.h"
    "include/UI/QTBoxLogger.h"
)

set(QT_UI_FILES
    "include/UI/MainWindow.ui"
    "include/UI/NavmeshEditorWidget.ui"
)

set(CS2_HEADER
    "include/CS2/Aimbot.h"
    "include/CS2/Button.h"
    "include/CS2/ConfigReader.h"
    "include/CS2/CS2AI.h"
    "include/CS2/GameInformationHandler.h"
    "include/CS2/MemoryManager.h"
    "include/CS2/MovementStrategy.h"
    "include/CS2/NavmeshPoints.h"
    "include/CS2/Offsets.h"
    "include/CS2/Triggerbot.h"
    "include/CS2/Constants.h"
)

set(HEADERS
    ${UTILITY_HEADER}
    ${UI_HEADER}
    ${CS2_HEADER}
)

set(CS2_SOURCE
    "src/CS2/Aimbot.cpp"
    "src/CS2/Button.cpp"
    "src/CS2/ConfigReader.cpp"
    "src/CS2/CS2AI.cpp"
    "src/CS2/GameInformationHandler.cpp"
    "src/CS2/MemoryManager.cpp"
    "src/CS2/MovementStrategy.cpp"
    "src/CS2/NavmeshPoints.cpp"
    "src/CS2/Offsets.cpp"
    "src/CS2/Triggerbot.cpp"
)

set(UI_SOURCE
    "src/UI/CS2Runner.cpp"
    "src/UI/MainWindow.cpp"
    "src/UI/NavmeshEditor.cpp"
    "src/UI/NavmeshEditorWidget.cpp"
    "src/UI/QTBoxLogger.cpp"
)

set(UTILITY_SOURCE
    "src/Utility/Logging.cpp"
    "src/Utility/Utility.cpp"
    "src/Utility/Dijkstra.cpp"
)

set(SOURCES
    ${CS2_SOURCE}
    ${UI_SOURCE}
    ${UTILITY_SOURCE}
)

# ========= Qt 自动处理开关 =========
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ========= VS 解决方案分组（可选，与你原来一致）=========
source_group("\\Header Files\\Utility" FILES ${UTILITY_HEADER})
source_group("\\Header Files\\UI" FILES ${UI_HEADER} ${QT_UI_FILES})
source_group("\\Header Files\\CS2" FILES ${CS2_HEADER})

source_group("\\Source Files\\Utility" FILES ${UTILITY_SOURCE})
source_group("\\Source Files\\UI" FILES ${UI_SOURCE})
source_group("\\Source Files\\CS2" FILES ${CS2_SOURCE})
source_group("\\Source Files" FILES "src/main.cpp")
source_group("Other" REGULAR_EXPRESSION "\\.h$|\\.cpp$|\\.stamp$|\\.rule$")

# ========= 目标、属性与链接 =========
add_executable(sg_tools
    "src/main.cpp"
    ${SOURCES}
    ${HEADERS}
    ${QT_UI_FILES}
)

# 生成的最终可执行文件名为 sg-tools(.exe)
set_target_properties(sg_tools PROPERTIES
    OUTPUT_NAME "sg-tools"
)

target_include_directories(sg_tools PUBLIC "include")

# VS 调试目录与启动项目
set_property(TARGET sg_tools PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT sg_tools)

target_link_libraries(sg_tools Qt6::Widgets Qt6::Network)

# 编译选项
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
if (MSVC)
    target_compile_options(sg_tools PRIVATE "/MP")
endif()

# ========= （可选但推荐）嵌入 Windows 版本资源，任务管理器“描述/产品名”里显示 sg-tools =========
if (WIN32)
    # 确保 res/app.rc 存在
    target_sources(sg_tools PRIVATE "res/app.rc")
endif()

# ========= 构建后复制资源目录到可执行文件同目录 =========
# 保留你原本复制 Configuration/ 和 Navmesh/ 的动作，但改成到运行目录
add_custom_command(TARGET sg_tools POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/Configuration"
            "$<TARGET_FILE_DIR:sg_tools>/Configuration"
)

add_custom_command(TARGET sg_tools POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/Navmesh"
            "$<TARGET_FILE_DIR:sg_tools>/Navmesh"
)

# =========（可选）自动部署 Qt 依赖（需有 windeployqt 宏/函数）=========
if (MSVC)
    # 若你的 include(windeployqt) 定义了该命令，则启用；否则可以注释掉
    if(COMMAND windeployqt)
        windeployqt(sg_tools)
    endif()
endif()
