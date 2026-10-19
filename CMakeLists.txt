cmake_minimum_required(VERSION 3.25)
project(kde-peripherals VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui)
include(GNUInstallDirs)

add_library(peripherals-core STATIC
    daemon/Hidraw.cpp
    daemon/Device.cpp
    daemon/Log.cpp
    daemon/Registry.cpp
    daemon/CompxProtocol.cpp
    daemon/CompxMouse.cpp
    daemon/HidppProtocol.cpp
    daemon/LogitechDevice.cpp
    daemon/RkProtocol.cpp
    daemon/RkKeyboard.cpp
    daemon/GamepadSource.cpp
)
target_include_directories(peripherals-core PUBLIC daemon)
target_link_libraries(peripherals-core PUBLIC Qt6::Core Qt6::DBus Qt6::Gui)

add_executable(peripheralsd daemon/main.cpp daemon/PeripheralsService.cpp)
target_link_libraries(peripheralsd PRIVATE peripherals-core)
install(TARGETS peripheralsd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if (CMAKE_INSTALL_PREFIX STREQUAL "/usr")
    set(_unit_dir "lib/systemd/user")
    set(_udev_dir "lib/udev/rules.d")
else()
    set(_unit_dir "${CMAKE_INSTALL_DATADIR}/systemd/user")
    set(_udev_dir "${CMAKE_INSTALL_LIBDIR}/udev/rules.d")
endif()
set(PERIPHERALS_SYSTEMD_USER_UNIT_DIR "${_unit_dir}" CACHE PATH "Where to install the systemd user unit")
set(PERIPHERALS_UDEV_RULES_DIR "${_udev_dir}" CACHE PATH "Where to install the udev rules")

configure_file(systemd/peripheralsd.service.in peripheralsd.service @ONLY)
configure_file(systemd/io.github.rodiada.Peripherals.service.in io.github.rodiada.Peripherals.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/peripheralsd.service DESTINATION ${PERIPHERALS_SYSTEMD_USER_UNIT_DIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/io.github.rodiada.Peripherals.service DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
set(PERIPHERALS_HELPER ${CMAKE_INSTALL_FULL_LIBEXECDIR}/kde-peripherals-bind-driver)
configure_file(udev/kde-peripherals-bind-driver.in kde-peripherals-bind-driver @ONLY)
configure_file(udev/70-kde-peripherals.rules.in 70-kde-peripherals.rules @ONLY)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/kde-peripherals-bind-driver DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/70-kde-peripherals.rules DESTINATION ${PERIPHERALS_UDEV_RULES_DIR})

find_program(MSGFMT_EXECUTABLE msgfmt REQUIRED)
file(GLOB PO_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/po/*.po)
set(MO_FILES)
foreach(kind mouse keyboard gamepad)
    set(plasmoid_id io.github.rodiada.peripherals.${kind})
    set(plasmoid_dir ${CMAKE_INSTALL_DATADIR}/plasma/plasmoids/${plasmoid_id})
    install(FILES plasmoid/${kind}/metadata.json DESTINATION ${plasmoid_dir})
    install(DIRECTORY plasmoid/ui DESTINATION ${plasmoid_dir}/contents)
    foreach(po ${PO_FILES})
        get_filename_component(lang ${po} NAME_WE)
        set(mo ${CMAKE_CURRENT_BINARY_DIR}/locale/${lang}/plasma_applet_${plasmoid_id}.mo)
        add_custom_command(
            OUTPUT ${mo}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/locale/${lang}
            COMMAND ${MSGFMT_EXECUTABLE} -o ${mo} ${po}
            DEPENDS ${po}
        )
        install(FILES ${mo} DESTINATION ${plasmoid_dir}/contents/locale/${lang}/LC_MESSAGES)
        list(APPEND MO_FILES ${mo})
    endforeach()
endforeach()
add_custom_target(translations ALL DEPENDS ${MO_FILES})

option(PERIPHERALS_TESTS "Build tests" OFF)
if (PERIPHERALS_TESTS)
    file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.cpp)
    foreach(test_source ${TEST_SOURCES})
        get_filename_component(test_name ${test_source} NAME_WE)
        string(REPLACE "_test" "-test" test_target ${test_name})
        add_executable(${test_target} ${test_source})
        target_link_libraries(${test_target} PRIVATE peripherals-core)
    endforeach()
endif()

set(CPACK_PACKAGE_NAME kde-peripherals)
set(CPACK_PACKAGE_VENDOR "Rodion Adamenok")
set(CPACK_PACKAGE_CONTACT "https://github.com/RodiAda/kde-peripherals")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "KDE Plasma tray widgets for gaming mice, keyboards and gamepads")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/RodiAda/kde-peripherals")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
set(CPACK_STRIP_FILES ON)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "plasma-workspace (>= 4:6.0), udev")
set(CPACK_DEBIAN_PACKAGE_SECTION kde)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE MIT)
set(CPACK_RPM_PACKAGE_REQUIRES "plasma-workspace >= 6.0, systemd-udev")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION /usr/lib/systemd /usr/lib/systemd/user /usr/lib/udev /usr/lib/udev/rules.d /usr/share/dbus-1 /usr/share/dbus-1/services /usr/share/plasma /usr/share/plasma/plasmoids)
include(CPack)
