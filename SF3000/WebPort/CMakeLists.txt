cmake_minimum_required(VERSION 3.20)

project(StarFighterWebPort LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(SF_BUILD_BROWSER "Build the Emscripten browser executable" OFF)

set(SF_WEB_PORT_SOURCES
    Collision.c
    Maths_Stuff.c
    Plot_Land.c
    SF_ARMAnim.c
    SF_ARMBurn.c
    SF_ARMCell_portable.c
    SF_ARMLink.c
    SF_ARMUtils.c
    sf_3do_compat.c
    sf3000_webport_laser.c
    sf3000_webport_plot_graphic.c
    sf3000_webport_sky.c
    sf3000_webport_smoke.c
    sf_web_port_renderer.c
    sf_web_runtime.c
)

add_library(starfighter_web_port STATIC ${SF_WEB_PORT_SOURCES})
target_compile_definitions(starfighter_web_port PUBLIC SF_WEB_PORT)
target_include_directories(starfighter_web_port
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

if(MSVC)
    target_compile_options(starfighter_web_port PRIVATE /W4 /WX)
    target_compile_options(starfighter_web_port PUBLIC /J)
else()
    target_compile_options(starfighter_web_port PRIVATE -Wall -Wextra -Werror)
    target_compile_options(starfighter_web_port PUBLIC -funsigned-char)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(portable_arm_tests
        test_portable_arm.c
        Maths_Stuff.c
        SF_ARMUtils.c
        SF_ARMLink.c
        Collision.c
    )
    add_test(NAME portable_arm_tests COMMAND portable_arm_tests)

    add_executable(terrain_port_tests Plot_Land_test.c)
    target_compile_definitions(terrain_port_tests PRIVATE PLOT_LAND_PORT_TEST)
    add_test(NAME terrain_port_tests COMMAND terrain_port_tests)

    add_executable(renderer_port_tests
        tests/test_renderer_ports.c
        sf3000_webport_plot_graphic.c
        sf3000_webport_sky.c
        sf3000_webport_laser.c
        sf3000_webport_smoke.c
    )
    add_test(NAME renderer_port_tests COMMAND renderer_port_tests)

    add_executable(three_do_compat_tests tests/test_3do_compat.c)
    target_link_libraries(three_do_compat_tests PRIVATE starfighter_web_port)
    add_test(NAME three_do_compat_tests COMMAND three_do_compat_tests)
    set_tests_properties(three_do_compat_tests PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../..")
endif()

if(SF_BUILD_BROWSER)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR
            "SF_BUILD_BROWSER requires the Emscripten toolchain. "
            "Configure with: emcmake cmake -S SF3000/WebPort -B <build-dir> "
            "-DSF_BUILD_BROWSER=ON")
    endif()

    # The original sources use a mixture of filename casing.  This overlay
    # lets an Emscripten/Linux build preserve those sources unchanged.
    set(SF_BROWSER_CASE_ALIAS_DIR "${CMAKE_CURRENT_BINARY_DIR}/case-aliases")
    if(NOT WIN32)
        file(MAKE_DIRECTORY "${SF_BROWSER_CASE_ALIAS_DIR}")

        function(sf_browser_case_alias alias target)
            get_filename_component(alias_directory "${alias}" DIRECTORY)
            if(NOT alias_directory STREQUAL "")
                file(MAKE_DIRECTORY "${SF_BROWSER_CASE_ALIAS_DIR}/${alias_directory}")
            endif()
            file(WRITE "${SF_BROWSER_CASE_ALIAS_DIR}/${alias}"
                "#include <${target}>\n")
        endfunction()

        foreach(platform_header IN ITEMS
            Audio BlockFile CPakSubscriber ControlSubscriber DataAcq
            DataStreamDebug DataStreamLib Debug3DO DSStreamHeader FontLib
            Graphics Init3DO Portfolio SATemplates SAudioSubscriber
            SoundPlayer TextLib Types UMemory Utils3DO
        )
            string(TOLOWER "${platform_header}" platform_header_lower)
            sf_browser_case_alias("${platform_header}.h" "${platform_header_lower}.h")
        endforeach()

        sf_browser_case_alias("String.h" "string.h")
        sf_browser_case_alias("Stdio.h" "stdio.h")
        sf_browser_case_alias("Event.h" "event.h")
        sf_browser_case_alias("Fontlib.h" "fontlib.h")
        sf_browser_case_alias("Textlib.h" "textlib.h")
        sf_browser_case_alias("SF_ArmAnim.h" "SF_ARMAnim.h")
        sf_browser_case_alias("SF_Armburn.h" "SF_ARMBurn.h")
        sf_browser_case_alias("SF_ArmCell.h" "SF_ARMCell.h")
        sf_browser_case_alias("SF_ArmLink.h" "SF_ARMLink.h")
        sf_browser_case_alias("SF_Armlink.h" "SF_ARMLink.h")
        sf_browser_case_alias("SF_ArmSky.h" "SF_ARMSky.h")
        sf_browser_case_alias("SF_ArmUtils.h" "SF_ARMUtils.h")
        sf_browser_case_alias("SF_CelUtils.h" "SF_Celutils.h")
        sf_browser_case_alias("SF_IO.h" "SF_Io.h")
        sf_browser_case_alias("SF_ScreenUtils.h" "SF_Screenutils.h")
        sf_browser_case_alias("SF_io.h" "SF_Io.h")
        sf_browser_case_alias("Sound_control.h" "Sound_Control.h")
        sf_browser_case_alias("bonus_control.h" "Bonus_Control.h")
        sf_browser_case_alias("camera_struct.h" "Camera_Struct.h")
        sf_browser_case_alias("draw_frame.h" "Draw_Frame.h")
        sf_browser_case_alias("explosion.h" "Explosion.h")
        sf_browser_case_alias("ship_control.h" "Ship_Control.h")
        sf_browser_case_alias("ship_struct.h" "Ship_Struct.h")
        sf_browser_case_alias("SFLib/SF_NVRam.h" "SFlib/SF_NVRam.h")
        sf_browser_case_alias("SFlib/SF_io.h" "SFlib/SF_Io.h")
    endif()

    set(SF_LEGACY_GAMEPLAY_SOURCES
        ../Star3000.c
        ../SFlib/Bit_Control.c
        ../SFlib/Bonus_Control.c
        ../SFlib/broker_shell.c
        ../SFlib/bs_joystick.c
        ../SFlib/Collision_Update.c
        ../SFlib/Draw_Frame.c
        ../SFlib/Draw_Land.c
        ../SFlib/Explosion.c
        ../SFlib/Graphics_Set.c
        ../SFlib/Ground_Control.c
        ../SFlib/Laser_Control.c
        ../SFlib/PlayCPakStream.c
        ../SFlib/PrepareStream.c
        ../SFlib/Rotate_Land.c
        ../SFlib/Setup_Tables.c
        ../SFlib/SF_Access.c
        ../SFlib/SF_Bonus.c
        ../SFlib/SF_Celutils.c
        ../SFlib/SF_Control.c
        ../SFlib/SF_Font.c
        ../SFlib/SF_Io.c
        ../SFlib/SF_Joystick.c
        ../SFlib/SF_Map.c
        ../SFlib/SF_Menu.c
        ../SFlib/SF_Message.c
        ../SFlib/SF_Music.c
        ../SFlib/SF_NVRam.c
        ../SFlib/SF_Palette.c
        ../SFlib/SF_Pyramid.c
        ../SFlib/SF_Screenutils.c
        ../SFlib/SF_Sound.c
        ../SFlib/SF_Status.c
        ../SFlib/SF_Utility.c
        ../SFlib/SF_Video.c
        ../SFlib/SF_War.c
        ../SFlib/Ship_Command.c
        ../SFlib/Ship_Control.c
        ../SFlib/Smoke_Control.c
        ../SFlib/Sound_Control.c
        ../SFlib/test_prog.c
        ../SFlib/Update_Frame.c
        ../SFlib/Weapons.c
    )

    add_executable(starfighter_web ${SF_LEGACY_GAMEPLAY_SOURCES})
    # Keep the generated Emscripten glue at the name loaded by web/index.html.
    # The generator expression prevents multi-config generators adding Debug/.
    set_target_properties(starfighter_web PROPERTIES
        OUTPUT_NAME starfighter
        RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_SOURCE_DIR}/web>"
    )
    target_compile_definitions(starfighter_web PRIVATE SF_WEB_PORT)
    target_include_directories(starfighter_web BEFORE PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${SF_BROWSER_CASE_ALIAS_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
        "${CMAKE_CURRENT_SOURCE_DIR}/../SFlib"
    )
    target_link_libraries(starfighter_web PRIVATE starfighter_web_port)
    target_link_options(starfighter_web PRIVATE
        "-sASYNCIFY"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sNO_EXIT_RUNTIME=1"
        "-sEXPORTED_RUNTIME_METHODS=UTF8ToString,HEAPU8"
    )

    # The normalized mirror is the only packaged file tree. It omits Music,
    # Video, Voices, and Samples while preserving raw CEL and mesh bitstreams.
    set(SF_WEB_ASSET_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../web-assets/runtime"
        CACHE PATH
        "Normalized non-media asset root to preload; set empty to omit assets")
    if(NOT SF_WEB_ASSET_ROOT STREQUAL "")
        cmake_path(ABSOLUTE_PATH SF_WEB_ASSET_ROOT
            BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            NORMALIZE
            OUTPUT_VARIABLE SF_WEB_ASSET_ROOT_ABSOLUTE)
        if(NOT IS_DIRECTORY "${SF_WEB_ASSET_ROOT_ABSOLUTE}/SF_Resources")
            message(FATAL_ERROR
                "SF_WEB_ASSET_ROOT must contain SF_Resources. Run the asset "
                "converter with --build-runtime or set SF_WEB_ASSET_ROOT to "
                "another normalized runtime root.")
        endif()
        foreach(media_directory IN ITEMS Music Video Voices Samples)
            if(IS_DIRECTORY
                    "${SF_WEB_ASSET_ROOT_ABSOLUTE}/SF_Resources/${media_directory}")
                message(FATAL_ERROR
                    "SF_WEB_ASSET_ROOT must be media-free; found "
                    "SF_Resources/${media_directory}.")
            endif()
        endforeach()
        file(TO_CMAKE_PATH
            "${SF_WEB_ASSET_ROOT_ABSOLUTE}/SF_Resources"
            SF_WEB_RESOURCES_DIRECTORY)
        target_link_options(starfighter_web PRIVATE
            "SHELL:--preload-file \"${SF_WEB_RESOURCES_DIRECTORY}@/\"")
    endif()

endif()
