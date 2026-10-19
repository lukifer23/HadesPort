cmake_minimum_required(VERSION 3.22)
project(hades_native LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(native_app_glue STATIC ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC ${ANDROID_NDK}/sources/android/native_app_glue)
set(LUA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../.local/native-deps/lua-5.2.2/src)
file(GLOB LUA_SOURCES CONFIGURE_DEPENDS ${LUA_DIR}/*.c)
list(FILTER LUA_SOURCES EXCLUDE REGEX "/lua(c)?\\.c$")
add_library(lua52 STATIC ${LUA_SOURCES})
target_include_directories(lua52 PUBLIC ${LUA_DIR})
target_compile_definitions(lua52 PRIVATE LUA_USE_POSIX)
add_library(hades_native SHARED engine.cpp lua_runtime.cpp lua_map.cpp lua_spawn.cpp source_value.cpp actor_runtime.cpp effect_animation.cpp effect_particle.cpp animation_registry.cpp hit_arguments.cpp effect_arguments.cpp self_effects.cpp effect_affect.cpp body_transition.cpp physics_runtime.cpp camera_fade.cpp movie_preload.cpp speed_effects.cpp effect_update.cpp weapon_execution.cpp projectile_runtime.cpp projectile_world.cpp unit_status.cpp actor_animation.cpp actor_marker.cpp primitive_properties.cpp asset_packages.cpp script_scheduler.cpp delayed_actions.cpp player.cpp recovered_runtime.cpp scene_runtime.cpp object_runtime.cpp weapon_runtime.cpp weapon_catalog.cpp text_runtime.cpp input_runtime.cpp map_creation.cpp graphics_scene.cpp scene_renderer.cpp scene_animation.cpp game_ui.cpp ui_renderer.cpp touch_controls.cpp camera_rig.cpp input_bindings.cpp control_bindings.cpp)
target_compile_options(hades_native PRIVATE -Wall -Wextra -Wpedantic -Werror=return-type)
target_link_options(hades_native PRIVATE -uANativeActivity_onCreate -Wl,-z,max-page-size=16384)
target_link_libraries(hades_native PRIVATE native_app_glue lua52 android log EGL GLESv3 z)

# Original compressed audio/event graphs and native API26 output.
target_sources(hades_native PRIVATE audio_world.cpp audio_channels.cpp audio_timeline.cpp audio_limiter.cpp audio_three_eq.cpp audio_chorus.cpp audio_sample_stream.cpp audio_bank_decoder.cpp audio_output.cpp)
include(${CMAKE_CURRENT_SOURCE_DIR}/../audio_decoder.cmake)
hades_link_audio_decoder(hades_native "${CMAKE_CURRENT_SOURCE_DIR}/../../.local/native-audio-build/android-arm64-api26-verified-v2/verification.json")

target_sources(hades_native PRIVATE lua_audio.cpp)

target_sources(hades_native PRIVATE lua_vfx.cpp)

target_sources(hades_native PRIVATE lua_actor_animation.cpp)
target_sources(hades_native PRIVATE action_effect.cpp lua_action_fx.cpp)
target_sources(hades_native PRIVATE screen_animation.cpp lua_screen_animation.cpp)
target_sources(hades_native PRIVATE unit_movement.cpp lua_player_input.cpp)

target_sources(hades_native PRIVATE actor_properties.cpp lua_lifecycle.cpp)
target_sources(hades_native PRIVATE actor_unit_lifecycle.cpp)
target_sources(hades_native PRIVATE lua_collision_notify.cpp)
target_sources(hades_native PRIVATE lua_stop_animation.cpp blocker_survey.cpp)
target_sources(hades_native PRIVATE lua_control_notify.cpp)
target_sources(hades_native PRIVATE lua_spatial_queries.cpp)
target_sources(hades_native PRIVATE native_camera.cpp lua_camera.cpp)
target_sources(hades_native PRIVATE lua_actor_motion.cpp unit_path.cpp)

target_sources(hades_native PRIVATE scene_primary.cpp lua_primaries.cpp)

target_sources(hades_native PRIVATE lua_combat.cpp actor_lifecycle.cpp)

target_sources(hades_native PRIVATE lua_physics.cpp self_effect_world.cpp)

target_sources(hades_native PRIVATE lua_effects.cpp lua_config_options.cpp)
target_sources(hades_native PRIVATE lua_projectiles.cpp)
target_sources(hades_native PRIVATE lua_presentation.cpp)
target_sources(hades_native PRIVATE lua_occlusion.cpp)
target_sources(hades_native PRIVATE lua_location.cpp actor_location.cpp actor_spatial.cpp)
target_sources(hades_native PRIVATE projectile_collision.cpp)
target_sources(hades_native PRIVATE actor_lighting.cpp)
target_sources(hades_native PRIVATE actor_occlusion.cpp)

target_sources(hades_native PRIVATE actor_light_world.cpp lua_lights.cpp)
target_sources(hades_native PRIVATE interaction_runtime.cpp lua_interaction.cpp)
target_sources(hades_native PRIVATE life_runtime.cpp)
target_sources(hades_native PRIVATE speech_world.cpp lua_speech.cpp)
target_sources(hades_native PRIVATE projectile_hit.cpp)

target_sources(hades_native PRIVATE text_layout.cpp text_caption.cpp lua_text.cpp)

target_sources(hades_native PRIVATE color_grading.cpp lua_color_grading.cpp)

target_sources(hades_native PRIVATE color_grading_renderer.cpp)

target_sources(hades_native PRIVATE bloom_state.cpp lua_bloom.cpp bloom_renderer.cpp)

# Original save pipeline (save agent): pinned Luabins 0.3 serializer and LZ4 1.9.4
# block codec from scripts/prepare_native_dependencies.py.
set(LUABINS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../.local/native-deps/luabins-0.3/src)
set(LZ4_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../.local/native-deps/lz4-1.9.4/lib)
add_library(luabins03 STATIC ${LUABINS_DIR}/luabins.c ${LUABINS_DIR}/save.c ${LUABINS_DIR}/load.c ${LUABINS_DIR}/savebuffer.c ${LUABINS_DIR}/write.c ${LUABINS_DIR}/luainternals.c)
target_include_directories(luabins03 PUBLIC ${LUABINS_DIR})
target_compile_options(luabins03 PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/luabins_lua52_compat.h)
target_link_libraries(luabins03 PUBLIC lua52)
add_library(lz4block STATIC ${LZ4_DIR}/lz4.c)
target_include_directories(lz4block PUBLIC ${LZ4_DIR})
target_sources(hades_native PRIVATE save_runtime.cpp lua_save.cpp)
target_link_libraries(hades_native PRIVATE luabins03 lz4block)

# Original Profile<N>.sjson / .ctrls persistence (config file agent).
target_sources(hades_native PRIVATE config_file.cpp)

target_sources(hades_native PRIVATE native_flash.cpp lua_flash.cpp)

target_sources(hades_native PRIVATE native_shake.cpp lua_shake.cpp lua_clock.cpp)
