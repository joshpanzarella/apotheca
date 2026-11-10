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

add_library(apotheca_engine STATIC engine/src/engine.cpp engine/src/drums.cpp engine/src/phazed.cpp)
target_link_libraries(apotheca_engine PRIVATE easycz)
target_include_directories(apotheca_engine PUBLIC engine/include)
target_compile_options(apotheca_engine PRIVATE -Wall -Wextra)

if(APOTHECA_BUILD_TESTS)
  enable_testing()
  add_executable(engine_test tests/engine_test.cpp)
  target_link_libraries(engine_test PRIVATE apotheca_engine)
  add_test(NAME engine COMMAND engine_test)
  add_executable(phazed_test tests/phazed_test.cpp)
  target_link_libraries(phazed_test PRIVATE apotheca_engine)
  add_test(NAME phazed COMMAND phazed_test)
endif()

# Renders a short piece to a WAV file, to hear the engine without the app.
add_executable(render_demo tools/render_demo.cpp)
target_link_libraries(render_demo PRIVATE apotheca_engine)

if(APOTHECA_BUILD_APP)
  find_package(SDL2 REQUIRED)
  find_package(Threads REQUIRED)
  add_executable(apotheca app/main.cpp app/input.cpp app/pattern_screen.cpp app/realtime.cpp app/text.cpp)
  target_include_directories(apotheca PRIVATE third_party)
  # stb_truetype is someone else's code; do not hold it to our warnings.
  set_source_files_properties(app/text.cpp PROPERTIES COMPILE_OPTIONS "-Wno-unused-function")
  if(APOTHECA_BUILD_TESTS)
    # The screen's button handling, driven without a window or a sound card.
    add_executable(ui_test tests/ui_test.cpp app/input.cpp app/pattern_screen.cpp app/text.cpp)
    target_include_directories(ui_test PRIVATE app third_party)
    add_test(NAME ui COMMAND ui_test)
  endif()
  target_compile_options(apotheca PRIVATE -Wall -Wextra)
  if(TARGET SDL2::SDL2)
    target_link_libraries(apotheca PRIVATE apotheca_engine SDL2::SDL2 Threads::Threads)
    if(TARGET ui_test)
      target_link_libraries(ui_test PRIVATE apotheca_engine SDL2::SDL2)
    endif()
  else()
    target_include_directories(apotheca PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(apotheca PRIVATE apotheca_engine ${SDL2_LIBRARIES} Threads::Threads)
    if(TARGET ui_test)
      target_include_directories(ui_test PRIVATE ${SDL2_INCLUDE_DIRS})
      target_link_libraries(ui_test PRIVATE apotheca_engine ${SDL2_LIBRARIES})
    endif()
  endif()
endif()
