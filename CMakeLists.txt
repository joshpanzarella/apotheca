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

# The FM core behind FM-6 (third_party/msfa, Apache 2.0; changes: third_party/CHANGES.md). Its
# fixed-point arithmetic lets numbers wrap round (phases), so it is built to wrap, never to guess.
add_library(msfa STATIC
  third_party/msfa/dx7note.cc third_party/msfa/env.cc third_party/msfa/exp2.cc
  third_party/msfa/fm_core.cc third_party/msfa/fm_op_kernel.cc third_party/msfa/freqlut.cc
  third_party/msfa/lfo.cc third_party/msfa/pitchenv.cc third_party/msfa/sin.cc)
target_compile_options(msfa PRIVATE -fwrapv -Wall -Wextra)

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
add_library(apotheca_engine STATIC engine/src/engine.cpp engine/src/effects.cpp engine/src/denormals.cpp engine/src/drums.cpp engine/src/phazed.cpp engine/src/sampler.cpp engine/src/jx3p.cpp engine/src/fm6.cpp engine/src/physic.cpp engine/src/acid.cpp engine/src/cloud.cpp engine/src/drum_synth.cpp
    ${APOTHECA_FX})
target_link_libraries(apotheca_engine PRIVATE easycz msfa)
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
  add_executable(song_test tests/song_test.cpp)
  target_link_libraries(song_test PRIVATE apotheca_engine)
  add_test(NAME song COMMAND song_test)
  add_executable(effects_test tests/effects_test.cpp)
  target_link_libraries(effects_test PRIVATE apotheca_engine)
  # In two halves (--part), each about half the time: whole, it was near its two minutes on a busy machine.
  add_test(NAME effects1 COMMAND effects_test --part 1)
  add_test(NAME effects2 COMMAND effects_test --part 2)
  add_executable(throw_test tests/throw_test.cpp)
  target_link_libraries(throw_test PRIVATE apotheca_engine)
  add_executable(glide_test tests/glide_test.cpp)
  target_link_libraries(glide_test PRIVATE apotheca_engine)
  add_test(NAME glide COMMAND glide_test)
  add_executable(chordfx_test tests/chordfx_test.cpp)
  target_link_libraries(chordfx_test PRIVATE apotheca_engine)
  add_test(NAME chordfx COMMAND chordfx_test)
  add_executable(load_test tests/load_test.cpp)
  target_link_libraries(load_test PRIVATE apotheca_engine)
  add_test(NAME load COMMAND load_test)
  add_test(NAME throw COMMAND throw_test)
  add_executable(feel_test tests/feel_test.cpp)
  target_link_libraries(feel_test PRIVATE apotheca_engine)
  add_test(NAME feel COMMAND feel_test)
  # CHOKE (drums.hpp, DrumMachine::setLaneChoke): two groups on a drum track, a hit cuts the rest of its group.
  add_executable(choke_test tests/choke_test.cpp)
  target_link_libraries(choke_test PRIVATE apotheca_engine)
  add_test(NAME choke COMMAND choke_test)
  # The JX-3P (docs/plans/jx3p-plan.md): the skeleton, its voices, tick, envelope and VCA. It reads
  # the synth's own parts (engine/src/jx3p/) and the shared measuring tools.
  add_executable(jx3p_test tests/jx3p_test.cpp)
  target_link_libraries(jx3p_test PRIVATE apotheca_engine)
  target_include_directories(jx3p_test PRIVATE engine/src tests)
  add_test(NAME jx3p COMMAND jx3p_test)
  # The JX-3P's DCOs (plan batch 2, engine/src/jx3p/dco.hpp): aliasing, the DCO table.
  add_executable(jx3p_dco_test tests/jx3p_dco_test.cpp)
  target_link_libraries(jx3p_dco_test PRIVATE apotheca_engine)
  target_include_directories(jx3p_dco_test PRIVATE engine/src tests)
  add_test(NAME jx3p_dco COMMAND jx3p_dco_test)
  # The JX-3P's VCF and HPF (plan batch 3, engine/src/jx3p/ir3109.hpp): the VCF table, the HPF, BRILLIANCE.
  add_executable(jx3p_vcf_test tests/jx3p_vcf_test.cpp)
  target_link_libraries(jx3p_vcf_test PRIVATE apotheca_engine)
  target_include_directories(jx3p_vcf_test PRIVATE engine/src tests)
  add_test(NAME jx3p_vcf COMMAND jx3p_vcf_test)
  # The JX-3P's LFO (plan batch 4, engine/src/jx3p/lfo.hpp): its table, shapes, delay and routes.
  add_executable(jx3p_lfo_test tests/jx3p_lfo_test.cpp)
  target_link_libraries(jx3p_lfo_test PRIVATE apotheca_engine)
  target_include_directories(jx3p_lfo_test PRIVATE engine/src tests)
  add_test(NAME jx3p_lfo COMMAND jx3p_lfo_test)
  # The JX-3P's chorus (plan batch 5, engine/src/jx3p/chorus.hpp): its delays, sweep, filters, fade and sleep.
  add_executable(jx3p_chorus_test tests/jx3p_chorus_test.cpp)
  target_link_libraries(jx3p_chorus_test PRIVATE apotheca_engine)
  target_include_directories(jx3p_chorus_test PRIVATE engine/src tests)
  add_test(NAME jx3p_chorus COMMAND jx3p_chorus_test)
  # The JX-3P's 32 factory patches (plan batch 7): the table against the booklet as read
  # (tests/jx3p_nam.inc), the booklet's three checks, every patch played.
  add_executable(jx3p_presets_test tests/jx3p_presets_test.cpp)
  target_link_libraries(jx3p_presets_test PRIVATE apotheca_engine)
  target_include_directories(jx3p_presets_test PRIVATE engine/src tests)
  add_test(NAME jx3p_presets COMMAND jx3p_presets_test)
  # FM-6 (docs/plans/fm6-plan.md): the 32 factory sounds, the pitch, the macros, the house rules.
  add_executable(fm6_test tests/fm6_test.cpp)
  target_link_libraries(fm6_test PRIVATE apotheca_engine)
  target_include_directories(fm6_test PRIVATE tests)
  add_test(NAME fm6 COMMAND fm6_test)
  # PHYSIC (docs/plans/physic-plan.md): the rule header, the modes, the string, the strike, the voices, the
  # dice and the house rules. It reads the synth's own parts (engine/src/physic/) and the measuring tools.
  add_executable(physic_test tests/physic_test.cpp)
  target_link_libraries(physic_test PRIVATE apotheca_engine)
  target_include_directories(physic_test PRIVATE engine/src tests)
  add_test(NAME physic COMMAND physic_test)
  # ACID (docs/plans/acid-plan.md): the rule header, the voice, accent and slide, the house rules.
  add_executable(acid_test tests/acid_test.cpp)
  target_link_libraries(acid_test PRIVATE apotheca_engine)
  target_include_directories(acid_test PRIVATE engine/src tests)
  add_test(NAME acid COMMAND acid_test)
  # CLOUD (docs/plans/granular-plan.md): the rule header, the synth, the house rules.
  add_executable(cloud_test tests/cloud_test.cpp)
  target_link_libraries(cloud_test PRIVATE apotheca_engine)
  target_include_directories(cloud_test PRIVATE engine/src tests)
  add_test(NAME cloud COMMAND cloud_test)
  # DRUM SYNTH (docs/plans/drumsynth-plan.md): the rule header and the twelve models' sounds.
  add_executable(drumsynth_test tests/drumsynth_test.cpp)
  target_link_libraries(drumsynth_test PRIVATE apotheca_engine)
  target_include_directories(drumsynth_test PRIVATE tests)
  add_test(NAME drumsynth COMMAND drumsynth_test)
  add_executable(mod_test tests/mod_test.cpp)
  target_link_libraries(mod_test PRIVATE apotheca_engine)
  add_test(NAME mod COMMAND mod_test)
  # The tests' temporary folders (tests/temp_folder.hpp): each deleted when its program ends, links
  # never followed; and no test source makes one another way (33,000 left behind filled the disk).
  add_executable(temp_test tests/temp_test.cpp)
  add_test(NAME temp COMMAND temp_test ${CMAKE_SOURCE_DIR})
  set_tests_properties(engine phazed song effects1 effects2 throw feel choke glide chordfx jx3p jx3p_dco jx3p_vcf jx3p_lfo jx3p_chorus jx3p_presets fm6 mod physic acid cloud drumsynth load temp PROPERTIES TIMEOUT 120)
endif()

# Renders a short piece to a WAV file, to hear the engine without the app.
add_executable(render_demo tools/render_demo.cpp)
target_link_libraries(render_demo PRIVATE apotheca_engine)

if(APOTHECA_BUILD_APP)
  find_package(SDL2 REQUIRED)
  find_package(Threads REQUIRED)
  # Everything of the app but main(): compiled ONCE, and linked by the program and by every test
  # of it. (It used to be compiled again for each of the nine tests: ten times in all.)
  add_library(apotheca_app STATIC app/app.cpp app/lane_edit.cpp app/bench_run.cpp app/chords.cpp app/controls.cpp app/sampler_screen.cpp app/settings_screen.cpp app/samples.cpp app/stretch.cpp app/stretch_smooth.cpp app/fx_screen.cpp app/input.cpp app/kit_screen.cpp app/note_screen.cpp app/pattern_screen.cpp app/pattern_slices.cpp
      app/instruments.cpp app/synth_screen.cpp app/mod_knob.cpp app/mod_screen.cpp app/recorder.cpp app/session.cpp app/session_songs.cpp app/session_synth.cpp app/session_samples.cpp app/song_file.cpp app/song_screen.cpp app/synth_spec.cpp app/presets.cpp app/transfer.cpp app/midi.cpp app/bounce.cpp app/sample_picker.cpp app/designer.cpp app/design_page.cpp
      app/text.cpp app/ui.cpp app/realtime.cpp app/redraw.cpp
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

  add_executable(apotheca app/main.cpp)
  target_compile_options(apotheca PRIVATE -Wall -Wextra)
  target_link_libraries(apotheca PRIVATE apotheca_app)

  # Timings, for keeping an eye on what things cost: ./build/bench
  add_executable(bench tools/bench.cpp)
  target_link_libraries(bench PRIVATE apotheca_app)

  if(APOTHECA_BUILD_TESTS)
    # The app driven without a window or a sound card: one test program for each part of it.
    foreach(name ui app save chord sample transfer design midi controls throwapp nextscene glideapp chordfxapp under scope hear mixer songend feelapp chokeapp picker bench modapp modknob modtab presets laneedit copybar drumsynthapp)
      add_executable(${name}_test tests/${name}_test.cpp)
      target_link_libraries(${name}_test PRIVATE apotheca_app)
      add_test(NAME ${name} COMMAND ${name}_test)
    endforeach()
    # A test that hangs is a failed test in two minutes, not a run that never ends.
    set_tests_properties(ui app save chord sample transfer design midi controls throwapp nextscene glideapp chordfxapp under scope hear mixer songend feelapp chokeapp picker bench modapp modknob modtab presets laneedit copybar drumsynthapp PROPERTIES TIMEOUT 120)
    # PHYSIC's added presets played through their whole sound (slots, sends, MODs), each held to a
    # loudness window, no clip, a tail that ends and MODs that move (docs/plans/physic-plan.md, section 17).
    add_test(NAME presets_heard COMMAND presets_test --heard)
    set_tests_properties(presets_heard PROPERTIES TIMEOUT 120)
    # FM-6's added presets the same way, each MOD heard too (docs/plans/fm6-plan.md, section 11).
    add_test(NAME presets_heard_fm6 COMMAND presets_test --heard-fm6)
    set_tests_properties(presets_heard_fm6 PROPERTIES TIMEOUT 120)
    # JAX-EP's added presets the same way, each with its folder's phrase, and the synth's own VCA clip
    # held at 0, at a placed note's level and at full velocity (docs/plans/jx3p-plan.md, "The second set").
    add_test(NAME presets_heard_jaxep COMMAND presets_test --heard-jaxep)
    set_tests_properties(presets_heard_jaxep PROPERTIES TIMEOUT 120)
    # ACID's (acid-plan.md, batch 4): an acid line of accents and slides, each MOD heard, the synth's clip at 0.
    add_test(NAME presets_heard_acid COMMAND presets_test --heard-acid)
    set_tests_properties(presets_heard_acid PROPERTIES TIMEOUT 120)
    # CLOUD's (granular-plan.md, batch 3): through the TONE and a recording, each MOD heard.
    add_test(NAME presets_heard_cloud COMMAND presets_test --heard-cloud)
    set_tests_properties(presets_heard_cloud PROPERTIES TIMEOUT 120)
    # The speed plan's changes (docs/plans/speed-plan.md), each held to the sound it made before,
    # to the bit, beside the old way of working it out. It reads the effects' and the CZ's own parts.
    add_executable(speed_test tests/speed_test.cpp)
    target_link_libraries(speed_test PRIVATE apotheca_app)
    target_include_directories(speed_test PRIVATE engine/src third_party/easycz)
    add_test(NAME speed COMMAND speed_test)
    set_tests_properties(speed PROPERTIES TIMEOUT 120)
    # The screen drawn only when it can have changed (app/redraw.hpp): a long session, every pass held
    # to the picture, and each reason taken away in turn, a third of them in each of idle1 to idle3.
    add_executable(idle_test tests/idle_test.cpp)
    target_link_libraries(idle_test PRIVATE apotheca_app)
    add_test(NAME idle1 COMMAND idle_test --part 1)
    add_test(NAME idle2 COMMAND idle_test --part 2)
    add_test(NAME idle3 COMMAND idle_test --part 3)
    set_tests_properties(idle1 idle2 idle3 PROPERTIES TIMEOUT 120)
    target_compile_definitions(chord_test PRIVATE APOTHECA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    # TEXT SIZE: the setting and its row; and every still the program knows, drawn by the program at all four sizes.
    add_executable(textsize_test tests/textsize_test.cpp)
    target_link_libraries(textsize_test PRIVATE apotheca_app)
    target_compile_definitions(textsize_test PRIVATE APOTHECA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME textsize COMMAND textsize_test)
    add_test(NAME textviews COMMAND textsize_test views $<TARGET_FILE:apotheca>)
    set_tests_properties(textsize textviews PROPERTIES TIMEOUT 120)
    # Each by itself: textviews starts the program a hundred times, four at once, and textsize draws
    # some thousands of screens. Beside the others they slow them (effects came to 116 s of its 120).
    set_tests_properties(textsize textviews PROPERTIES RUN_SERIAL TRUE)
    # THEME: the six palettes, the setting and its row; and every still drawn by the program in all six.
    add_executable(theme_test tests/theme_test.cpp)
    target_link_libraries(theme_test PRIVATE apotheca_app)
    add_test(NAME theme COMMAND theme_test)
    add_test(NAME themeviews COMMAND theme_test views $<TARGET_FILE:apotheca>)
    set_tests_properties(theme themeviews PROPERTIES TIMEOUT 120)
    set_tests_properties(themeviews PROPERTIES RUN_SERIAL TRUE) # (a hundred and more starts of the program, four at once)
    # The handheld's launcher: an update is put in place, or taken back out.
    add_test(NAME launcher COMMAND sh ${CMAKE_SOURCE_DIR}/tests/launcher_test.sh ${CMAKE_SOURCE_DIR}/scripts/Apotheca.sh)
    # SELECT is drawn as SEL: no text the device shows spells it out.
    add_test(NAME words COMMAND sh ${CMAKE_SOURCE_DIR}/tests/words_test.sh ${CMAKE_SOURCE_DIR}/app)
    # The demo render is run and read (nothing else in ctest runs tools/render_demo.cpp).
    add_test(NAME demo COMMAND render_demo ${CMAKE_BINARY_DIR}/demo_test.wav)
    set_tests_properties(demo PROPERTIES PASS_REGULAR_EXPRESSION "bass: Phazed, [^\n]+\nchords: JAX-EP, String I\n20\\.7 seconds, peak 0\\.[5-9]")
    set_tests_properties(launcher words demo PROPERTIES TIMEOUT 120)
  endif()
endif()
