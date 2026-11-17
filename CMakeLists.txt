cmake_minimum_required(VERSION 3.24)
project(DeepMotorTool VERSION 1.0.1 LANGUAGES CXX)

# 工程版本是界面、Windows 文件属性、发布清单和压缩包名称的唯一来源。
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md" RELEASE_NOTES)
string(FIND "${RELEASE_NOTES}" "## V${PROJECT_VERSION}" RELEASE_NOTES_VERSION_POSITION)
if(RELEASE_NOTES_VERSION_POSITION EQUAL -1)
    message(FATAL_ERROR "CHANGELOG.md must contain release notes for v${PROJECT_VERSION}")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/VERSION.txt" "V${PROJECT_VERSION}\n")
configure_file(resources/branding/version.rc.in
               "${CMAKE_CURRENT_BINARY_DIR}/version.rc" @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network PrintSupport Test)
qt_standard_project_setup()

add_library(qcustomplot STATIC
    third_party/qcustomplot/qcustomplot.h
    third_party/qcustomplot/qcustomplot.cpp
)
target_include_directories(qcustomplot PUBLIC third_party/qcustomplot)
target_link_libraries(qcustomplot PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::PrintSupport)
if(MSVC)
    target_compile_options(qcustomplot PRIVATE /wd4996)
else()
    target_compile_options(qcustomplot PRIVATE -Wno-deprecated-declarations)
endif()

add_library(motor_core STATIC
    src/domain/Types.h
    src/protocol/CommandCatalog.h src/protocol/CommandCatalog.cpp
    src/protocol/StatusWordCatalog.h src/protocol/StatusWordCatalog.cpp
    src/protocol/CanFdFrameCodec.h src/protocol/CanFdFrameCodec.cpp
    src/protocol/EtherCanV1Codec.h src/protocol/EtherCanV1Codec.cpp
    src/protocol/Protocol.h src/protocol/LegacyCanFdCodec.h src/protocol/LegacyCanFdCodec.cpp
    src/protocol/ProtocolRegistry.h src/protocol/ProtocolRegistry.cpp
    src/transport/Transport.h src/transport/SimulatedTransport.h src/transport/SimulatedTransport.cpp
    src/transport/UdpTransport.h src/transport/UdpTransport.cpp
    src/transport/TransportFactory.h src/transport/TransportFactory.cpp
    src/services/DeviceService.h src/services/DeviceService.cpp
    src/services/AsyncDeviceService.h src/services/AsyncDeviceService.cpp
    src/services/HostConfiguration.h src/services/HostConfiguration.cpp
    src/services/DeviceDiscovery.h src/services/DeviceDiscovery.cpp
    src/services/FrameDiagnostics.h src/services/FrameDiagnostics.cpp
    src/services/RecordingService.h src/services/RecordingService.cpp
    src/services/DeviceRegistry.h src/services/DeviceRegistry.cpp
)
target_include_directories(motor_core PUBLIC src)
target_link_libraries(motor_core PUBLIC Qt6::Core Qt6::Network)

add_library(device_simulator_core STATIC
    src/simulator/DeviceSimulatorServer.h src/simulator/DeviceSimulatorServer.cpp
)
target_include_directories(device_simulator_core PUBLIC src)
target_link_libraries(device_simulator_core PUBLIC motor_core Qt6::Core Qt6::Network)

qt_add_executable(device-simulator src/simulator/main.cpp)
target_link_libraries(device-simulator PRIVATE device_simulator_core Qt6::Core)
set_target_properties(device-simulator PROPERTIES OUTPUT_NAME "deep-device-simulator")

# 真机验证工具不加入 CTest；只有操作员明确传入 --motion 才会让真机运动。
qt_add_executable(hardware-probe tests/HardwareProbe.cpp)
target_link_libraries(hardware-probe PRIVATE motor_core Qt6::Core Qt6::Network)
set_target_properties(hardware-probe PROPERTIES WIN32_EXECUTABLE FALSE OUTPUT_NAME "deep-hardware-probe")

qt_add_executable(motor-studio
    src/app/main.cpp
    resources/branding/deep-motor-tool.rc
    "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
    src/ui/MainWindow.h src/ui/MainWindow.cpp
    src/ui/ReleaseNotesDialog.h src/ui/ReleaseNotesDialog.cpp
    src/ui/Localization.h src/ui/Localization.cpp
    src/ui/PlotWidget.h src/ui/PlotWidget.cpp
    src/ui/Pages.h src/ui/PageWidgets.h src/ui/Theme.h src/ui/ThemeColors.h
    src/ui/pages/MotionPage.cpp src/ui/pages/CalibrationPage.cpp
    src/ui/pages/LinearityPage.cpp src/ui/pages/AngleErrorPage.cpp src/ui/pages/InspectionPage.cpp
    src/ui/pages/TemperaturePage.cpp src/ui/pages/IdentityPage.cpp
    src/ui/pages/ResponsePage.cpp src/ui/pages/EventLogPage.cpp
    src/ui/pages/ParametersDialog.cpp
    src/ui/AutoConnectDialog.h src/ui/AutoConnectDialog.cpp
    src/ui/DeviceManagerDialog.h src/ui/DeviceManagerDialog.cpp
)
qt_add_resources(motor-studio "theme_assets"
    PREFIX "/"
    FILES
        resources/icons/chevron-down.svg
        resources/icons/chevron-up.svg
        resources/icons/check.svg
        resources/branding/deep-robotics.svg
        resources/branding/deep-robotics-light.svg
        resources/branding/deep-app-icon.svg
)
qt_add_resources(motor-studio "release_notes"
    PREFIX "/"
    FILES CHANGELOG.md
)
target_link_libraries(motor-studio PRIVATE motor_core qcustomplot Qt6::Widgets)
target_compile_definitions(motor-studio PRIVATE APP_VERSION="${PROJECT_VERSION}")
set_target_properties(motor-studio PROPERTIES WIN32_EXECUTABLE TRUE OUTPUT_NAME "deep-motor-tool")
if(MSVC)
    target_compile_options(motor_core PRIVATE /W4 /utf-8)
    target_compile_options(motor-studio PRIVATE /W4 /utf-8)
else()
    target_compile_options(motor_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(motor-studio PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(DEEP_ENABLE_GCC_ANALYZER "Run GCC's static analyzer on first-party sources" OFF)
if(DEEP_ENABLE_GCC_ANALYZER AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # qcustomplot 是外部依赖，不对它运行 -fanalyzer；该选项只检查自有代码。
    target_compile_options(motor_core PRIVATE -fanalyzer)
    target_compile_options(motor-studio PRIVATE -fanalyzer)
endif()

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(core-tests tests/CoreTests.cpp)
    target_link_libraries(core-tests PRIVATE motor_core Qt6::Test)
    add_test(NAME core-tests COMMAND core-tests)
    qt_add_executable(plot-widget-tests
        tests/PlotWidgetTests.cpp
        src/ui/PlotWidget.h src/ui/PlotWidget.cpp
        src/ui/Localization.h src/ui/Localization.cpp
    )
    target_include_directories(plot-widget-tests PRIVATE src)
    target_link_libraries(plot-widget-tests PRIVATE qcustomplot Qt6::Widgets Qt6::Test)
    add_test(NAME plot-widget-tests COMMAND plot-widget-tests -platform offscreen)
    qt_add_executable(udp-device-integration-tests
        tests/UdpDeviceIntegrationTests.cpp
        src/ui/PlotWidget.h src/ui/PlotWidget.cpp
        src/ui/Localization.h src/ui/Localization.cpp
    )
    target_include_directories(udp-device-integration-tests PRIVATE src)
    target_link_libraries(udp-device-integration-tests PRIVATE
        device_simulator_core qcustomplot Qt6::Widgets Qt6::Test)
    add_test(NAME udp-device-integration-tests COMMAND udp-device-integration-tests -platform offscreen)
    add_test(NAME ui-smoke COMMAND motor-studio --smoke-test -platform offscreen)
    add_test(NAME ui-smoke-en COMMAND motor-studio --smoke-test --language en_US -platform offscreen)
    add_test(NAME ui-lifecycle COMMAND motor-studio --lifecycle-test -platform offscreen)
endif()
configure_file(config.ini ${CMAKE_CURRENT_BINARY_DIR}/config.ini COPYONLY)
install(TARGETS motor-studio device-simulator RUNTIME DESTINATION bin)
install(FILES config.ini DESTINATION bin RENAME config.default.ini)
install(FILES CHANGELOG.md "${CMAKE_CURRENT_BINARY_DIR}/VERSION.txt" DESTINATION .)
qt_generate_deploy_app_script(TARGET motor-studio OUTPUT_SCRIPT deploy_script NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${deploy_script})
