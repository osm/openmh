add_library(mh_lap_presets STATIC src/engine/game/lap_presets.c)
target_include_directories(mh_lap_presets PUBLIC src)
mh_c89(mh_lap_presets)

add_library(mh_input STATIC
	src/engine/input/device_choices.c
	src/engine/input/clipboard.c
	src/engine/input/provider.c
	src/engine/input/keyboard.c
	src/engine/input/devices.c
	src/engine/input/text_edit.c
	src/engine/input/binding.c
	src/engine/input/drive_controls.c
	src/engine/input/tick_input.c
	src/engine/input/view_controls.c
	src/engine/input/screenshot.c
	src/engine/input/control_type.c
	src/engine/input/chat.c
	src/engine/input/lobby_console.c
	src/engine/input/lobby_keys.c)
target_include_directories(mh_input PUBLIC src)
target_link_libraries(mh_input PUBLIC mh_clock mh_text)
mh_c89(mh_input)

target_link_libraries(mh_input PRIVATE mh_input_build mh_lap_presets)

add_library(mh_settings STATIC
	src/engine/settings/screenshot.c
	src/engine/settings/av.c
	src/engine/settings/parse.c
	src/engine/settings/course_music.c
	src/engine/settings/config.c
	src/engine/settings/config_write.c
	src/engine/settings/control_layout.c
	src/engine/settings/menu_sound.c
	src/engine/settings/control_apply.c
	src/engine/settings/control_write.c
	src/engine/settings/decimal.c
	src/engine/settings/control_catalog.c
	src/engine/settings/address_book.c
	src/engine/settings/menu_return.c)
target_include_directories(mh_settings PUBLIC src)
target_link_libraries(mh_settings PUBLIC mh_text mh_error)
mh_c89(mh_settings)

target_link_libraries(mh_settings PRIVATE
	mh_arguments
	mh_core
	mh_screenshot_build)

add_library(mh_standings_order STATIC src/engine/game/standings_order.c)
target_link_libraries(mh_standings_order PUBLIC mh_core)
mh_c89(mh_standings_order)

add_library(mh_game STATIC
	src/engine/game/cheats.c
	src/engine/game/highscore.c
	src/engine/game/highscore_awards.c
	src/engine/game/league.c
	src/engine/game/league_catalog.c
	src/engine/game/league_names.c
	src/engine/game/league_race.c
	src/engine/game/league_document.c
	src/engine/game/league_tracks.c
	src/engine/game/league_unlocks.c
	src/engine/game/quick_race.c
	src/engine/game/race_records.c
	src/engine/game/result_prepare.c
	src/engine/game/standings.c)
target_include_directories(mh_game PUBLIC src)
target_link_libraries(mh_game PUBLIC mh_assets mh_standings_order)
mh_c89(mh_game)

add_library(mh_route_geometry STATIC
	src/engine/simulation/route_curve.c
	src/engine/simulation/route_projection.c)
target_link_libraries(mh_route_geometry PUBLIC mh_assets)
mh_c89(mh_route_geometry)

add_library(mh_sim STATIC
	src/engine/simulation/ai_driver.c
	src/engine/simulation/ai_path.c
	src/engine/simulation/ai_personality.c
	src/engine/simulation/ai_roster.c
	src/engine/simulation/ai_traffic.c
	src/engine/simulation/body.c
	src/engine/simulation/body_effects.c
	src/engine/simulation/body_points.c
	src/engine/simulation/camera.c
	src/engine/simulation/collision.c
	src/engine/simulation/contact_schedule.c
	src/engine/simulation/drive.c
	src/engine/simulation/drive_camera.c
	src/engine/simulation/drive_clock.c
	src/engine/simulation/drive_recovery.c
	src/engine/simulation/drive_setup.c
	src/engine/simulation/drive_particles.c
	src/engine/simulation/engine.c
	src/engine/simulation/feedback.c
	src/engine/simulation/feedback_sample.c
	src/engine/simulation/field.c
	src/engine/simulation/field_ai.c
	src/engine/simulation/field_camera.c
	src/engine/simulation/field_clock.c
	src/engine/simulation/field_contacts.c
	src/engine/simulation/field_objects.c
	src/engine/simulation/field_results.c
	src/engine/simulation/field_setup.c
	src/engine/simulation/field_tuning.c
	src/engine/simulation/finish_controls.c
	src/engine/simulation/forces.c
	src/engine/simulation/frame_time.c
	src/engine/simulation/tick_clock.c
	src/engine/simulation/grip.c
	src/engine/simulation/impact.c
	src/engine/simulation/intro_camera.c
	src/engine/simulation/motion.c
	src/engine/simulation/motion_rotation.c
	src/engine/simulation/motion_wrapper.c
	src/engine/simulation/object.c
	src/engine/simulation/opponent.c
	src/engine/simulation/pair.c
	src/engine/simulation/particles.c
	src/engine/simulation/placement.c
	src/engine/simulation/race.c
	src/engine/simulation/race_tuning.c
	src/engine/simulation/recovery.c
	src/engine/simulation/skids.c
	src/engine/simulation/suspension.c
	src/engine/simulation/suspension_force.c
	src/engine/simulation/track_camera.c
	src/engine/simulation/tyre_effects.c
	src/engine/simulation/vehicle_placement.c
	src/engine/simulation/vehicle.c
	src/engine/simulation/wheels.c)
target_include_directories(mh_sim PUBLIC src)
target_link_libraries(mh_sim PUBLIC
	mh_assets
	mh_clock
	mh_game
	mh_route_geometry)
mh_c89(mh_sim)

add_library(mh_window_system STATIC
	src/engine/window/window.c
	src/engine/window/title.c)
target_include_directories(mh_window_system PUBLIC src)
target_link_libraries(mh_window_system PUBLIC mh_error)
mh_c89(mh_window_system)

add_library(mh_graphics_catalog STATIC src/engine/window/graphics_catalog.c)
target_link_libraries(mh_graphics_catalog PUBLIC mh_window_system mh_ui)
mh_c89(mh_graphics_catalog)

add_library(mh_demo STATIC
	src/engine/demo/sample.c
	src/engine/demo/playback.c
	src/engine/demo/progress.c
	src/engine/demo/record.c
	src/engine/demo/benchmark.c)
target_link_libraries(mh_demo PUBLIC mh_net)
mh_c89(mh_demo)

add_library(mh_demo_sim STATIC
	src/engine/demo/race.c
	src/engine/demo/race_setup.c
	src/engine/demo/capture.c
	src/engine/demo/ghost_capture.c
	src/engine/demo/network_capture.c)
target_link_libraries(mh_demo_sim PUBLIC mh_demo mh_net_sim)
mh_c89(mh_demo_sim)
