cmake_minimum_required(VERSION 3.16)
project(apotheca VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(APOTHECA_BUILD_APP "Build the SDL2 app (needs SDL2)" ON)
option(APOTHECA_BUILD_TESTS "Build the engine tests" ON)

# The engine: no SDL, no device code. Keep it that way so it stays testable.
# The engine behind Phazed, copied unchanged from joshpanzarella/schwung-easycz.
set(CMAKE_C_STANDARD 11)
add_library(easycz STATIC
  third_party/easycz/dsp/cz_chorus.c third_party/easycz/dsp/cz_output.c
  third_party/easycz/dsp/cz_presets.c third_party/easycz/dsp/cz_synth.c
  third_party/easycz/dsp/cz_tone.c third_party/easycz/dsp/upd933.c)
target_include_directories(easycz PUBLIC third_party/easycz)
find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
  target_link_libraries(easycz PUBLIC ${MATH_LIBRARY})
endif()

# The version shown on screen: 0.N, N being the number of changes kept so far plus this one.
execute_process(COMMAND git rev-list --count HEAD WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE APOTHECA_CHANGES OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT APOTHECA_CHANGES)
  set(APOTHECA_CHANGES 0)
endif()
math(EXPR APOTHECA_CHANGES "${APOTHECA_CHANGES} + 1")
string(TIMESTAMP APOTHECA_BUILT "%Y-%m-%d")
# (Given to one small file only, app/version.cpp, so that a new number does not rebuild everything.)

# The effects: one file each, and every .cpp in the folder is built (a new effect needs no line here).
file(GLOB APOTHECA_FX CONFIGURE_DEPENDS engine/src/fx/*.cpp)
add_library(apotheca_engine STATIC engine/src/engine.cpp engine/src/effects.cpp engine/src/denormals.cpp engine/src/drums.cpp engine/src/phazed.cpp engine/src/sampler.cpp engine/src/twin.cpp engine/src/qmod.cpp
    ${APOTHECA_FX})
target_link_libraries(apotheca_engine PRIVATE easycz)
target_include_directories(apotheca_engine PUBLIC engine/include)
target_include_directories(apotheca_engine PRIVATE third_party)
target_compile_options(apotheca_engine PRIVATE -Wall -Wextra)

if(APOTHECA_BUILD_TESTS)
  enable_testing()
  add_executable(engine_test tests/engine_test.cpp)
  target_link_libraries(engine_test PRIVATE apotheca_engine)
  add_test(NAME engine COMMAND engine_test)
  add_executable(phazed_test tests/phazed_test.cpp)
  target_link_libraries(phazed_test PRIVATE apotheca_engine)
  add_test(NAME phazed COMMAND phazed_test)
  add_executable(twin_test tests/twin_test.cpp)
  add_executable(qmod_test tests/qmod_test.cpp)
  target_link_libraries(twin_test PRIVATE apotheca_engine)
  target_link_libraries(qmod_test PRIVATE apotheca_engine)
  add_test(NAME twin COMMAND twin_test)
  add_test(NAME qmod COMMAND qmod_test)
  add_executable(song_test tests/song_test.cpp)
  target_link_libraries(song_test PRIVATE apotheca_engine)
  add_test(NAME song COMMAND song_test)
  add_executable(effects_test tests/effects_test.cpp)
  target_link_libraries(effects_test PRIVATE apotheca_engine)
  add_test(NAME effects COMMAND effects_test)
  add_executable(throw_test tests/throw_test.cpp)
  target_link_libraries(throw_test PRIVATE apotheca_engine)
  add_executable(glide_test tests/glide_test.cpp)
  target_link_libraries(glide_test PRIVATE apotheca_engine)
  add_test(NAME glide COMMAND glide_test)
  add_executable(chordfx_test tests/chordfx_test.cpp)
  target_link_libraries(chordfx_test PRIVATE apotheca_engine)
  add_test(NAME chordfx COMMAND chordfx_test)
  add_test(NAME throw COMMAND throw_test)
  set_tests_properties(engine phazed twin qmod song effects throw glide chordfx PROPERTIES TIMEOUT 120)
endif()

# Renders a short piece to a WAV file, to hear the engine without the app.
add_executable(render_demo tools/render_demo.cpp)
target_link_libraries(render_demo PRIVATE apotheca_engine)

if(APOTHECA_BUILD_APP)
  find_package(SDL2 REQUIRED)
  find_package(Threads REQUIRED)
  # Everything of the app but main(): compiled ONCE, and linked by the program and by every test
  # of it. (It used to be compiled again for each of the nine tests: ten times in all.)
  add_library(apotheca_app STATIC app/app.cpp app/bench_run.cpp app/chords.cpp app/controls.cpp app/sampler_screen.cpp app/settings_screen.cpp app/samples.cpp app/stretch.cpp app/stretch_smooth.cpp app/fx_screen.cpp app/input.cpp app/kit_screen.cpp app/note_screen.cpp app/pattern_screen.cpp app/pattern_slices.cpp
      app/instruments.cpp app/synth_screen.cpp app/recorder.cpp app/session.cpp app/session_songs.cpp app/session_synth.cpp app/session_samples.cpp app/song_file.cpp app/song_screen.cpp app/synth_spec.cpp app/transfer.cpp app/midi.cpp app/bounce.cpp app/sample_picker.cpp app/designer.cpp app/design_page.cpp
      app/text.cpp app/ui.cpp
      app/version.cpp)
  target_include_directories(apotheca_app PUBLIC app third_party)
  target_compile_options(apotheca_app PRIVATE -Wall -Wextra)
  # stb_truetype is someone else's code; do not hold it to our warnings.
  set_source_files_properties(app/text.cpp PROPERTIES COMPILE_OPTIONS "-Wno-unused-function")
  set_source_files_properties(app/version.cpp PROPERTIES COMPILE_DEFINITIONS "APOTHECA_VERSION=\"0.${APOTHECA_CHANGES}\";APOTHECA_BUILT=\"${APOTHECA_BUILT}\"")
  if(TARGET SDL2::SDL2)
    target_link_libraries(apotheca_app PUBLIC apotheca_engine SDL2::SDL2 Threads::Threads)
  else()
    target_include_directories(apotheca_app PUBLIC ${SDL2_INCLUDE_DIRS})
    target_link_libraries(apotheca_app PUBLIC apotheca_engine ${SDL2_LIBRARIES} Threads::Threads)
  endif()

  add_executable(apotheca app/main.cpp app/realtime.cpp)
  target_compile_options(apotheca PRIVATE -Wall -Wextra)
  target_link_libraries(apotheca PRIVATE apotheca_app)

  # Timings, for keeping an eye on what things cost: ./build/bench
  add_executable(bench tools/bench.cpp)
  target_link_libraries(bench PRIVATE apotheca_app)

  if(APOTHECA_BUILD_TESTS)
    # The app driven without a window or a sound card: one test program for each part of it.
    foreach(name ui app save chord sample transfer design midi controls throwapp nextscene glideapp chordfxapp under)
      add_executable(${name}_test tests/${name}_test.cpp)
      target_link_libraries(${name}_test PRIVATE apotheca_app)
      add_test(NAME ${name} COMMAND ${name}_test)
    endforeach()
    # A test that hangs is a failed test in two minutes, not a run that never ends.
    set_tests_properties(ui app save chord sample transfer design midi controls throwapp nextscene glideapp chordfxapp under PROPERTIES TIMEOUT 120)
    target_compile_definitions(chord_test PRIVATE APOTHECA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    # TEXT SIZE: the setting and its row; and every still the program knows, drawn by the program at all four sizes.
    add_executable(textsize_test tests/textsize_test.cpp)
    target_link_libraries(textsize_test PRIVATE apotheca_app)
    target_compile_definitions(textsize_test PRIVATE APOTHECA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME textsize COMMAND textsize_test)
    add_test(NAME textviews COMMAND textsize_test views $<TARGET_FILE:apotheca>)
    set_tests_properties(textsize textviews PROPERTIES TIMEOUT 120)
    # It starts the program a hundred times, four at once: by itself, or the tests beside it take twice as long (effects came to 116 s of its 120).
    set_tests_properties(textviews PROPERTIES RUN_SERIAL TRUE)
    # The handheld's launcher: an update is put in place, or taken back out.
    add_test(NAME launcher COMMAND sh ${CMAKE_SOURCE_DIR}/tests/launcher_test.sh ${CMAKE_SOURCE_DIR}/scripts/Apotheca.sh)
    # SELECT is drawn as SEL: no text the device shows spells it out.
    add_test(NAME words COMMAND sh ${CMAKE_SOURCE_DIR}/tests/words_test.sh ${CMAKE_SOURCE_DIR}/app)
  endif()
endif()
