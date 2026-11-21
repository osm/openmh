add_library(mh_environment_esd STATIC
	src/backends/audio/esd_load.c
	src/backends/audio/esd_source.c)
target_link_libraries(mh_environment_esd
	PUBLIC
		mh_environment_loading
	PRIVATE
		mh_assets
		mh_resource_source)
mh_c89(mh_environment_esd)

add_library(mh_horn_chf STATIC
	src/backends/audio/chf_catalog.c
	src/backends/audio/chf_source.c)
target_link_libraries(mh_horn_chf
	PUBLIC
		mh_horn_loading
	PRIVATE
		mh_assets
		mh_resource_source)
mh_c89(mh_horn_chf)

add_library(mh_horn_fallback STATIC src/backends/audio/horn_fallback.c)
target_link_libraries(mh_horn_fallback
	PUBLIC
		mh_horn_loading
	PRIVATE
		mh_horn_chf
		mh_demo_asset_layout)
mh_c89(mh_horn_fallback)

add_library(mh_audio_fallback STATIC src/backends/audio/decoder_fallback.c)
target_link_libraries(mh_audio_fallback
	PUBLIC
		mh_audio_decoding
	PRIVATE
		mh_audio_wave
		mh_audio_assets
		mh_demo_asset_layout)
mh_c89(mh_audio_fallback)

if(MH_DECODE_OPUS)
	target_link_libraries(mh_audio_fallback PRIVATE mh_audio_ffmpeg)
	target_compile_definitions(mh_audio_fallback PRIVATE MH_DECODE_OPUS=1)
endif()

add_library(mh_audio_samples STATIC src/engine/audio/sample.c)
target_include_directories(mh_audio_samples PUBLIC src)
target_link_libraries(mh_audio_samples PUBLIC mh_audio_decoding)
mh_c89(mh_audio_samples)

add_library(mh_audio_decoding STATIC src/engine/audio/decoder.c)
target_include_directories(mh_audio_decoding PUBLIC src)
target_link_libraries(mh_audio_decoding PUBLIC mh_error)
mh_c89(mh_audio_decoding)

# Decoded sample arithmetic has no dependency on containers or playback.
add_library(mh_audio_pcm STATIC src/engine/audio/pcm.c)
target_include_directories(mh_audio_pcm PUBLIC src)
target_link_libraries(mh_audio_pcm PUBLIC mh_core)
mh_c89(mh_audio_pcm)

add_library(mh_audio STATIC
	src/engine/audio/ambient.c
	src/engine/audio/catalog.c
	src/engine/audio/cd.c
	src/engine/audio/crash.c
	src/engine/audio/effect_buffer.c
	src/engine/audio/effect_output.c
	src/engine/audio/effect_pcm.c
	src/engine/audio/effects.c
	src/engine/audio/effects_render.c
	src/engine/audio/engine_sound.c
	src/engine/audio/menu_sound.c
	src/engine/audio/mixer.c
	src/engine/audio/mixer_pcm.c
	src/engine/audio/mixer_spatial.c
	src/engine/audio/pause_sound.c
	src/engine/audio/race_cues.c
	src/engine/audio/race_sound.c
	src/engine/audio/reverb.c
	src/engine/audio/sound_pool.c
	src/engine/audio/sound_spatial.c
	src/engine/audio/sound_variant.c
	src/engine/audio/spatial_effects.c
	src/engine/audio/spatial_bank.c
	src/engine/audio/spatial_buffer.c
	src/engine/audio/spatial_levels.c
	src/engine/audio/spatial_listener.c
	src/engine/audio/spatial_pcm.c
	src/engine/audio/spatial_playback.c
	src/engine/audio/spatial_scene.c
	src/engine/audio/spatial_queue.c
	src/engine/audio/spatial_source.c
	src/engine/audio/surface.c
	src/engine/audio/surface_parameters.c
	src/engine/audio/surface_history.c
	src/engine/audio/vehicle_sound.c)

if("ds3d" IN_LIST MH_AUDIO_PROFILES)
	target_sources(mh_audio PRIVATE
		src/engine/audio/ds3d_listener.c
		src/engine/audio/ds3d_pcm.c
		src/engine/audio/ds3d_playback.c
		src/engine/audio/ds3d_queue.c
		src/engine/audio/ds3d_source.c
		src/engine/audio/ds3d_effects.c)
endif()

if("eax" IN_LIST MH_AUDIO_PROFILES)
	target_sources(mh_audio PRIVATE
		src/engine/audio/eax_effects.c
		src/engine/audio/eax_listener.c
		src/engine/audio/eax_pcm.c
		src/engine/audio/eax_playback.c
		src/engine/audio/eax_queue.c
		src/engine/audio/eax_reverb.c
		src/engine/audio/eax_source.c)
endif()

if("a3d12" IN_LIST MH_AUDIO_PROFILES)
	target_sources(mh_audio PRIVATE
		src/engine/audio/a3d12_effects.c
		src/engine/audio/a3d12_pcm.c
		src/engine/audio/a3d12_listener.c
		src/engine/audio/a3d12_playback.c
		src/engine/audio/a3d12_queue.c
		src/engine/audio/a3d12_source.c)
endif()

if("a3d20" IN_LIST MH_AUDIO_PROFILES)
	target_sources(mh_audio PRIVATE
		src/engine/audio/a3d20_effects.c
		src/engine/audio/a3d20_acoustics.c
		src/engine/audio/a3d20_paths.c
		src/engine/audio/a3d20_geometry.c
		src/engine/audio/a3d20_listener.c
		src/engine/audio/a3d20_pcm.c
		src/engine/audio/a3d20_playback.c
		src/engine/audio/a3d20_queue.c
		src/engine/audio/a3d20_sample.c
		src/engine/audio/a3d20_source.c)
endif()
target_link_libraries(mh_audio PUBLIC
	mh_audio_pcm
	mh_sim
	mh_settings
	mh_clock_source)
mh_c89(mh_audio)

add_library(mh_environment_loading STATIC src/engine/audio/environment_load.c)
target_link_libraries(mh_environment_loading PUBLIC mh_error)
mh_c89(mh_environment_loading)

add_library(mh_horn STATIC src/engine/audio/horn.c)
target_link_libraries(mh_horn PUBLIC mh_core)
mh_c89(mh_horn)

add_library(mh_horn_loading STATIC src/engine/audio/horn_load.c)
target_link_libraries(mh_horn_loading PUBLIC mh_horn mh_resource_source)
mh_c89(mh_horn_loading)

add_library(mh_audio_wave STATIC
	src/backends/audio/wave_decoder.c
	src/backends/audio/wave_metadata.c)
target_include_directories(mh_audio_wave PUBLIC src)
target_link_libraries(mh_audio_wave
	PUBLIC
		mh_audio_decoding
	PRIVATE
		mh_assets
		mh_resource_source)
mh_c89(mh_audio_wave)
include(cmake/ffmpeg.cmake)

add_library(mh_audio_ffmpeg STATIC)
target_link_libraries(mh_audio_ffmpeg PUBLIC mh_audio_decoding)

if(MH_HAVE_FFMPEG AND (MH_DECODE_OPUS OR MH_DECODE_MOVIES))
	target_sources(mh_audio_ffmpeg PRIVATE
		src/backends/audio/ffmpeg_decoder.c
		src/backends/audio/ffmpeg_stream.c
		src/backends/audio/ffmpeg_decode.c)
	target_link_libraries(mh_audio_ffmpeg PRIVATE mh_ffmpeg_input)
else()
	target_sources(mh_audio_ffmpeg PRIVATE src/backends/audio/ffmpeg_stub.c)
endif()
mh_c89(mh_audio_ffmpeg)

add_library(mh_audio_ffmpeg_encoding STATIC)
target_link_libraries(mh_audio_ffmpeg_encoding PUBLIC mh_audio_decoding)

if(MH_HAVE_FFMPEG AND MH_ENCODE_MEDIA)
	target_sources(mh_audio_ffmpeg_encoding PRIVATE
		src/backends/audio/ffmpeg_encode.c
		src/backends/audio/ffmpeg_encode_stream.c)
	target_link_libraries(mh_audio_ffmpeg_encoding PRIVATE mh_ffmpeg_output)
else()
	target_sources(mh_audio_ffmpeg_encoding PRIVATE
		src/backends/audio/ffmpeg_encode_stub.c)
endif()
mh_c89(mh_audio_ffmpeg_encoding)

add_library(mh_audio_assets STATIC
	src/engine/audio/sample_path.c
	src/engine/audio/ambient_assets.c
	src/engine/audio/menu_sound_assets.c
	src/engine/audio/music.c
	src/engine/audio/music_asset.c
	src/engine/audio/music_catalog.c
	src/engine/audio/pause_sound_assets.c
	src/engine/audio/race_sound_assets.c
	src/engine/audio/surface_assets.c)
target_include_directories(mh_audio_assets PUBLIC src)
target_link_libraries(mh_audio_assets PUBLIC
	mh_material_loading
	mh_environment_loading
	mh_audio_decoding
	mh_audio_samples
	mh_audio
	mh_clock_source
	mh_asset_loaders)
mh_c89(mh_audio_assets)

# Playback coordination and output dispatch have no device SDK dependency.
add_library(mh_audio_playback STATIC
	src/engine/audio/device.c
	src/engine/audio/menu_playback.c
	src/engine/audio/race_playback.c
	src/engine/audio/race_output.c
	src/engine/audio/race_profile.c
	src/engine/audio/race_effects.c
	src/engine/audio/race_events.c)
target_include_directories(mh_audio_playback PUBLIC src)
target_link_libraries(mh_audio_playback PUBLIC mh_audio_assets)
mh_c89(mh_audio_playback)

# Only the composition registry references concrete race mixers.
set(audio_registry_includes)
string(CONCAT audio_registry
	"/* Generated race profile registry. */\n"
	"#include \"engine/audio/race_effects.h\"\n"
	"const struct mh_race_effect_ops *\n"
	"mh_race_profile_ops(int selection)\n"
	"{\n"
	"    switch (selection) {\n")
string(CONCAT audio_choices
	"/* Generated profile availability; no mixer dependencies. */\n"
	"#include \"engine/audio/race_profile.h\"\n"
	"int\n"
	"mh_race_audio_available(unsigned int selection)\n"
	"{\n"
	"    switch (selection) {\n")

foreach(profile IN LISTS MH_AUDIO_PROFILES)
	string(TOUPPER "${profile}" upper)
	target_sources(mh_audio_playback PRIVATE
		src/engine/audio/${profile}.c)
	string(APPEND audio_registry_includes
		"#include \"engine/audio/${profile}.h\"\n")
	string(APPEND audio_registry
		"    case MH_RACE_AUDIO_${upper}: return &mh_${profile}_race_effects;\n")
	string(APPEND audio_choices
		"    case MH_RACE_AUDIO_${upper}: return 1;\n")
endforeach()
string(APPEND audio_registry "    default: return NULL;\n    }\n}\n")
string(APPEND audio_choices "    default: return 0;\n    }\n}\n")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/profiles/audio_registry.c"
	CONTENT "${audio_registry_includes}${audio_registry}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/profiles/audio_choices.c"
	CONTENT "${audio_choices}")
target_sources(mh_audio_playback PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}/profiles/audio_registry.c")

add_library(mh_audio_profiles STATIC
	src/engine/audio/profile_choices.c
	"${CMAKE_CURRENT_BINARY_DIR}/profiles/audio_choices.c")
target_include_directories(mh_audio_profiles PUBLIC src)
mh_c89(mh_audio_profiles)
target_link_libraries(mh_audio_playback PUBLIC mh_audio_profiles)
target_link_libraries(mh_ui PUBLIC mh_audio_profiles)
