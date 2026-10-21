# Native tracker decoder pinned to upstream d1b97ed (0.8.9.1 development snapshot).
# Compile private copies of the two filesystem-using loaders with ambient reads disabled.
include(FetchContent)
FetchContent_Declare(modplug
    URL https://codeload.github.com/Konstanty/libmodplug/tar.gz/d1b97ed0020bc620a059d3675d1854b40bd2608d
    URL_HASH SHA256=d145910ca10c3ecab0019d45be7e31ea6be1d1ee951ef0e1f747c9752d20f043
    SOURCE_SUBDIR scua-unused-upstream-build
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(modplug)
set(scua_modplug_private "${CMAKE_BINARY_DIR}/scua-modplug-private")
file(MAKE_DIRECTORY "${scua_modplug_private}/include/libmodplug")
configure_file("${modplug_SOURCE_DIR}/src/modplug.h" "${scua_modplug_private}/include/libmodplug/modplug.h" COPYONLY)
file(GLOB scua_modplug_sources CONFIGURE_DEPENDS "${modplug_SOURCE_DIR}/src/*.cpp")
foreach(loader load_abc load_pat)
    set(original "${modplug_SOURCE_DIR}/src/${loader}.cpp")
    file(READ "${original}" contents)
    string(REGEX REPLACE "#define _mm_fopen\\(name,mode\\)[^\n]*fopen\\(name,mode\\)"
        "#define _mm_fopen(name,mode) nullptr // SCUA fork: no ambient filesystem reads." patched "${contents}")
    if(patched STREQUAL contents)
        message(FATAL_ERROR "libmodplug filesystem patch did not match ${loader}")
    endif()
    if(loader STREQUAL "load_abc")
        string(REPLACE "fp = fopen(name, mode);" "fp = nullptr; // SCUA fork: resource bytes only, no file includes." patched "${patched}")
    endif()
    file(GENERATE OUTPUT "${scua_modplug_private}/${loader}.cpp" CONTENT "${patched}")
    list(REMOVE_ITEM scua_modplug_sources "${original}")
    list(APPEND scua_modplug_sources "${scua_modplug_private}/${loader}.cpp")
endforeach()
add_library(scua_modplug STATIC ${scua_modplug_sources})
target_compile_features(scua_modplug PRIVATE cxx_std_17)
target_compile_definitions(scua_modplug PRIVATE MODPLUG_BUILD HAVE_STDINT_H HAVE_STRINGS_H HAVE_SINF PUBLIC MODPLUG_STATIC)
target_include_directories(scua_modplug PRIVATE "${modplug_SOURCE_DIR}/src" "${modplug_SOURCE_DIR}/src/libmodplug"
    PUBLIC "${scua_modplug_private}/include")
set_target_properties(scua_modplug PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
target_link_libraries(lovedep::Modplug INTERFACE scua_modplug)
