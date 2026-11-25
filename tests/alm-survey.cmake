# Optional CMake project include for offline LEVEL MATCH diagnostics.
# Pass -DCMAKE_PROJECT_MasAudio_INCLUDE=<absolute path to this file>.
# The normal 1.8 test target and all plugin targets are unchanged by default.
function(mas_enable_alm_survey)
    if(NOT TARGET MasterSpace18Tests)
        message(FATAL_ERROR "Configure with MAS_BUILD_TESTS=ON for ALM survey")
    endif()
    get_target_property(sources MasterSpace18Tests SOURCES)
    list(FILTER sources EXCLUDE REGEX "(^|/)space_plugin_tests\\.cpp$")
    list(APPEND sources "${CMAKE_SOURCE_DIR}/tests/alm_calibration_survey.cpp")
    set_property(TARGET MasterSpace18Tests PROPERTY SOURCES "${sources}")
endfunction()
cmake_language(DEFER CALL mas_enable_alm_survey)
