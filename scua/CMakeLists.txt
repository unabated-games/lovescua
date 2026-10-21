set(SCUA_ROOT "" CACHE PATH "SCUA 0.37 SDK (contains include/scua.h and lib/libscua.a)")
find_path(SCUA_INCLUDE_DIR scua.h HINTS "${SCUA_ROOT}/include" REQUIRED)
find_library(SCUA_LIBRARY NAMES scua HINTS "${SCUA_ROOT}/lib" REQUIRED)
add_library(scua_runtime STATIC IMPORTED)
set_target_properties(scua_runtime PROPERTIES
    IMPORTED_LOCATION "${SCUA_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${SCUA_INCLUDE_DIR}")

# Reuse upstream's source lists, retaining the engine implementations and removing
# only the Lua adapters. Unused upstream modules are excluded from this build.
get_property(engine_targets DIRECTORY "${love_SOURCE_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
foreach(engine_target IN LISTS engine_targets)
    set_target_properties(${engine_target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
endforeach()
set(core_targets love_common love_data love_event_root
    love_joystick_root love_joystick_sdl love_sensor_root love_sensor_sdl
    love_mouse_root love_mouse_sdl
    love_filesystem_root love_filesystem_physfs
    love_font_root love_font_freetype love_graphics_root love_graphics_opengl
    love_image_root love_image_magpie love_math love_timer love_window_root love_window_sdl)
if(APPLE)
    list(APPEND core_targets love_graphics_metal)
    # Upstream's Xcode project enables ARC. Metal's retained encoders and
    # buffers depend on it; upstream CMake does not set the equivalent flag.
    target_compile_options(love_graphics_metal PRIVATE -fobjc-arc)
else()
    message(FATAL_ERROR "The initial SCUA host is currently supported on macOS only.")
endif()
if(LOVE_SCUA_AUDIO)
    list(APPEND core_targets love_audio_root love_audio_openal love_sound_root love_sound_lullaby)
endif()
foreach(engine_target IN LISTS core_targets)
    get_target_property(engine_sources ${engine_target} SOURCES)
    list(FILTER engine_sources EXCLUDE REGEX "/wrap_[^/]+$")
    list(FILTER engine_sources EXCLUDE REGEX "/(runtime|Reference)\\.cpp$")
    set_property(TARGET ${engine_target} PROPERTY SOURCES ${engine_sources})
endforeach()

set(scua_thread_support
    ../src/modules/thread/threads.cpp
    ../src/modules/thread/sdl/threads.cpp
    ../src/modules/thread/sdl/Thread.cpp)
add_executable(lovescua
    main.cpp bindings.cpp graphics.cpp platform.cpp objects.cpp math.cpp
    data.cpp audio.cpp audio_controls.cpp batches.cpp physics.cpp drawables.cpp input.cpp video.cpp rendering.cpp buffers.cpp textures.cpp fonts.cpp particles.cpp
    ${scua_thread_support}
    ../src/modules/video/VideoStream.cpp
    ../src/modules/video/theora/OggDemuxer.cpp
    ../src/modules/video/theora/TheoraVideoStream.cpp)
if(LOVE_SCUA_AUDIO)
    target_compile_definitions(lovescua PRIVATE LOVE_SCUA_AUDIO)
endif()
set_target_properties(lovescua PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_compile_definitions(lovescua PRIVATE LOVE_SCUA_HOST)
# Both the build and installed host find facades relative to their executable.
add_custom_target(scua_modules
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/lib"
        "$<TARGET_FILE_DIR:lovescua>/lovescua-modules"
    COMMENT "Copying SCUA runtime modules")
if(LOVE_SCUA_AUDIO AND LOVE_SCUA_FETCH_MODPLUG)
    add_custom_command(TARGET scua_modules POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:lovescua>/lovescua-modules/licenses"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${modplug_SOURCE_DIR}/COPYING"
            "$<TARGET_FILE_DIR:lovescua>/lovescua-modules/licenses/libmodplug.txt")
    install(FILES "${modplug_SOURCE_DIR}/COPYING" DESTINATION share/lovescua/licenses RENAME libmodplug.txt COMPONENT SCUA)
endif()
add_dependencies(lovescua scua_modules)
install(TARGETS lovescua RUNTIME DESTINATION bin COMPONENT SCUA)
install(DIRECTORY lib/ DESTINATION share/lovescua COMPONENT SCUA)
install(FILES "${love_SOURCE_DIR}/license.txt" DESTINATION share/lovescua COMPONENT SCUA)
target_compile_options(lovescua PRIVATE -Wall -Wextra)
target_link_libraries(lovescua PRIVATE scua_runtime ${core_targets}
    love_3p_glad love_3p_glslang love_3p_spirv_cross love_3p_ddsparse
    love_3p_box2d love_3p_wuff love_3p_lodepng love_3p_lz4 love_3p_noise1234 love_3p_physfs love_3p_xxhash
    lovedep::SDL lovedep::Zlib lovedep::Theora lovedep::Ogg)
if(APPLE)
    target_link_libraries(lovescua PRIVATE "-framework Security" "-framework CoreServices")
endif()

if(BUILD_TESTING)
    add_executable(scua_test_input test_input.cpp input.cpp ${scua_thread_support})
    get_target_property(scua_host_libraries lovescua LINK_LIBRARIES)
    target_link_libraries(scua_test_input PRIVATE ${scua_host_libraries})
    add_dependencies(lovescua scua_test_input)
    add_test(NAME scua_virtual_input COMMAND scua_test_input)
    set_tests_properties(scua_virtual_input PROPERTIES TIMEOUT 30)
    add_test(NAME scua_bounce COMMAND lovescua --no-input --verify --frames 120 --fixed-dt 0.016666666666666666
        "${love_SOURCE_DIR}/examples/bounce/main.scua")
    set_tests_properties(scua_bounce PROPERTIES TIMEOUT 30)
    add_test(NAME scua_bounce_opengl COMMAND lovescua --no-input --renderer opengl --verify --frames 120
        --fixed-dt 0.016666666666666666 "${love_SOURCE_DIR}/examples/bounce/main.scua")
    set_tests_properties(scua_bounce_opengl PROPERTIES TIMEOUT 30)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_executable(scua_test_ogg_audio test_ogg_audio.cpp)
    target_compile_features(scua_test_ogg_audio PRIVATE cxx_std_17)
    add_test(NAME scua_ogg_audio COMMAND scua_test_ogg_audio)
    set_tests_properties(scua_ogg_audio PROPERTIES LABELS "no-window")
    if(LOVE_SCUA_AUDIO)
        add_executable(scua_test_codecs test_codecs.cpp ${scua_thread_support})
        target_link_libraries(scua_test_codecs PRIVATE love_sound_lullaby love_sound_root love_data love_common lovedep::SDL)
        add_test(NAME scua_codecs COMMAND scua_test_codecs
            "${love_SOURCE_DIR}/examples/audio_controls/res/tone.mod"
            "${love_SOURCE_DIR}/examples/audio_controls/res/sample.ogv")
        set_tests_properties(scua_codecs PROPERTIES TIMEOUT 15 LABELS "no-window")
        add_test(NAME scua_audio_controls COMMAND lovescua --no-input --verify --frames 3
            --fixed-dt 0.016666666666666666 "${love_SOURCE_DIR}/examples/audio_controls")
        set_tests_properties(scua_audio_controls PROPERTIES TIMEOUT 45 ENVIRONMENT "ALSOFT_DRIVERS=null")
        if(TARGET scua_modplug)
            add_test(NAME scua_modplug_no_files COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/test_modplug.py" "$<TARGET_FILE:scua_modplug>")
            set_tests_properties(scua_modplug_no_files PROPERTIES TIMEOUT 15 LABELS "no-window")
        endif()
    endif()
    add_test(NAME scua_script_check COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_check.py" "$<TARGET_FILE:lovescua>")
    set_tests_properties(scua_script_check PROPERTIES TIMEOUT 45 LABELS "no-window")
    add_test(NAME scua_host_errors COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_host.py" "$<TARGET_FILE:lovescua>")
    set_tests_properties(scua_host_errors PROPERTIES TIMEOUT 90)
    set(SCUA_LICENSE "${love_SOURCE_DIR}/../scua/packaging/LICENSE" CACHE FILEPATH "Matching SCUA SDK license for packaging checks")
    if(EXISTS "${SCUA_LICENSE}")
        add_test(NAME scua_package_relocation COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_package.py" "$<TARGET_FILE:lovescua>" "${SCUA_LICENSE}")
        set_tests_properties(scua_package_relocation PROPERTIES TIMEOUT 90 LABELS "packaging;graphics")
        add_test(NAME scua_compute_package COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_package.py" "$<TARGET_FILE:lovescua>" "${SCUA_LICENSE}"
            "${love_SOURCE_DIR}/examples/compute")
        set_tests_properties(scua_compute_package PROPERTIES TIMEOUT 90 LABELS "packaging;graphics")
    endif()
    add_test(NAME scua_filesystem COMMAND lovescua --no-input --verify --frames 3
        --fixed-dt 0.016666666666666666 --save-dir "${CMAKE_BINARY_DIR}/test-saves"
        "${love_SOURCE_DIR}/examples/filesystem")
    set_tests_properties(scua_filesystem PROPERTIES TIMEOUT 30)
    set(scua_audio_test_arg "")
    if(NOT LOVE_SCUA_AUDIO)
        set(scua_audio_test_arg --no-audio)
    endif()
    add_test(NAME scua_native_boundary COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_native.py" "$<TARGET_FILE:lovescua>" ${scua_audio_test_arg})
    set_tests_properties(scua_native_boundary PROPERTIES TIMEOUT 90)
    find_program(SCUA_EXECUTABLE scua REQUIRED)
    add_test(NAME scua_sprite_controller COMMAND "${SCUA_EXECUTABLE}"
        "${love_SOURCE_DIR}/examples/sprite/controller_test.scua")
    foreach(renderer metal opengl)
        add_test(NAME scua_sprites_${renderer} COMMAND lovescua --no-input --renderer ${renderer} --verify
            --frames 120 --fixed-dt 0.016666666666666666 "${love_SOURCE_DIR}/examples/sprite")
        set_tests_properties(scua_sprites_${renderer} PROPERTIES TIMEOUT 30)
        add_test(NAME scua_features_${renderer} COMMAND lovescua --no-input --renderer ${renderer} --verify
            --frames 3 --fixed-dt 0.016666666666666666 "${love_SOURCE_DIR}/examples/features")
        set_tests_properties(scua_features_${renderer} PROPERTIES TIMEOUT 30)
        foreach(example math particles events mesh rendering instancing compute textures fonts_particles physics_controls)
            add_test(NAME scua_${example}_${renderer} COMMAND lovescua --no-input --renderer ${renderer} --verify
                --frames 3 --fixed-dt 0.016666666666666666 "${love_SOURCE_DIR}/examples/${example}")
            set_tests_properties(scua_${example}_${renderer} PROPERTIES TIMEOUT 30)
        endforeach()
        foreach(example contacts joints)
            add_test(NAME scua_${example}_${renderer} COMMAND lovescua --no-input --renderer ${renderer} --verify
                --frames 180 --fixed-dt 0.016666666666666666 "${love_SOURCE_DIR}/examples/${example}")
            set_tests_properties(scua_${example}_${renderer} PROPERTIES TIMEOUT 30)
        endforeach()
        add_test(NAME scua_video_${renderer} COMMAND lovescua --no-input --renderer ${renderer} --verify
            --frames 30 --fixed-dt 0.016666666666666666 "${love_SOURCE_DIR}/examples/video")
        set_tests_properties(scua_video_${renderer} PROPERTIES TIMEOUT 30 ENVIRONMENT "ALSOFT_DRIVERS=null")
        add_test(NAME scua_physics_${renderer} COMMAND lovescua --no-input --renderer ${renderer} --verify
            --frames 180 --fixed-dt 0.016666666666666666 "${love_SOURCE_DIR}/examples/physics")
        set_tests_properties(scua_physics_${renderer} PROPERTIES TIMEOUT 30)
        if(LOVE_SCUA_AUDIO)
            add_test(NAME scua_audio_queue_${renderer} COMMAND lovescua --no-input --renderer ${renderer} --verify
                --frames 3 --fixed-dt 0.016666666666666666 "${love_SOURCE_DIR}/examples/audio_queue")
            set_tests_properties(scua_audio_queue_${renderer} PROPERTIES TIMEOUT 30 ENVIRONMENT "ALSOFT_DRIVERS=null")
            add_test(NAME scua_audio_${renderer} COMMAND lovescua --no-input --renderer ${renderer} --verify
                --frames 3 --fixed-dt 0.016666666666666666 "${love_SOURCE_DIR}/examples/audio")
            set_tests_properties(scua_audio_${renderer} PROPERTIES TIMEOUT 30 ENVIRONMENT "ALSOFT_DRIVERS=null")
        endif()
        add_test(NAME scua_images_${renderer} COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_images.py" "$<TARGET_FILE:lovescua>" ${renderer})
        set_tests_properties(scua_images_${renderer} PROPERTIES TIMEOUT 90)
    endforeach()
endif()
