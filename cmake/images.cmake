add_library(mh_image_encoding STATIC src/engine/image/encode.c)
target_include_directories(mh_image_encoding PUBLIC src)
target_link_libraries(mh_image_encoding PUBLIC mh_error)
mh_c89(mh_image_encoding)

add_library(mh_image_decoding STATIC src/engine/image/decode.c)
target_include_directories(mh_image_decoding PUBLIC src)
target_link_libraries(mh_image_decoding PUBLIC mh_error)
mh_c89(mh_image_decoding)

add_library(mh_portrait_pixels STATIC src/engine/image/portrait.c)
target_include_directories(mh_portrait_pixels PUBLIC src)
target_link_libraries(mh_portrait_pixels PUBLIC mh_error)
mh_c89(mh_portrait_pixels)

add_library(mh_image_loading STATIC
	src/engine/image/load.c
	src/engine/image/portrait_load.c
	src/engine/image/track_map.c)
target_link_libraries(mh_image_loading PUBLIC
	mh_image_decoding
	mh_resource_source
	mh_portrait_pixels)
mh_c89(mh_image_loading)

add_library(mh_image_collections STATIC
	src/engine/image/collection.c
	src/engine/image/collection_load.c)
target_include_directories(mh_image_collections PUBLIC src)
target_link_libraries(mh_image_collections PUBLIC mh_error mh_image_decoding)
mh_c89(mh_image_collections)

add_library(mh_atlas_map STATIC
	src/engine/image/atlas_map.c
	src/engine/image/atlas_load.c)
target_include_directories(mh_atlas_map PUBLIC src)
target_link_libraries(mh_atlas_map PUBLIC mh_error PRIVATE mh_text)
mh_c89(mh_atlas_map)

add_library(mh_palette_loading STATIC src/engine/image/palette_load.c)
target_link_libraries(mh_palette_loading PUBLIC mh_error mh_resource_source)
mh_c89(mh_palette_loading)

add_library(mh_font_loading STATIC src/engine/font/source.c)
target_include_directories(mh_font_loading PUBLIC src)
target_link_libraries(mh_font_loading PUBLIC mh_error)
mh_c89(mh_font_loading)

add_library(mh_sprite_collections STATIC src/engine/sprite/collection.c)
target_include_directories(mh_sprite_collections PUBLIC src)
target_link_libraries(mh_sprite_collections PUBLIC mh_error)
mh_c89(mh_sprite_collections)

add_library(mh_palette_act STATIC src/backends/image/act_source.c)
target_link_libraries(mh_palette_act
	PUBLIC
		mh_palette_loading
	PRIVATE
		mh_assets
		mh_resource_source)
mh_c89(mh_palette_act)

add_library(mh_sprite_spr STATIC src/backends/sprite/spr_source.c)
target_include_directories(mh_sprite_spr PUBLIC src)
target_link_libraries(mh_sprite_spr
	PUBLIC
		mh_sprite_collections
	PRIVATE
		mh_assets
		mh_resource_source)
mh_c89(mh_sprite_spr)

add_library(mh_font_fnt STATIC src/backends/font/fnt_source.c)
target_include_directories(mh_font_fnt PUBLIC src)
target_link_libraries(mh_font_fnt
	PUBLIC
		mh_font_loading
	PRIVATE
		mh_assets
		mh_resource_source)
mh_c89(mh_font_fnt)

add_library(mh_font_fallback STATIC src/backends/font/fallback.c)
target_link_libraries(mh_font_fallback
	PUBLIC
		mh_font_loading
	PRIVATE
		mh_font_fnt
		mh_demo_asset_layout
		mh_demo_definitions)
mh_c89(mh_font_fallback)

add_library(mh_image_tga STATIC
	src/backends/image/tga_encoder.c
	src/backends/image/tga_decoder.c
	src/backends/image/tga_portrait.c
	src/backends/image/tga_thumbnail.c)
target_link_libraries(mh_image_tga
	PUBLIC
		mh_image_encoding
		mh_image_decoding
	PRIVATE
		mh_assets)
mh_c89(mh_image_tga)

add_library(mh_image_png STATIC
	EXCLUDE_FROM_ALL
	src/backends/image/png_encoder.c)
target_link_libraries(mh_image_png
	PUBLIC
		mh_image_encoding
	PRIVATE
		mh_core
		ZLIB::ZLIB)
mh_c89(mh_image_png)

add_library(mh_image_ilbm STATIC src/backends/image/ilbm_decoder.c)
target_link_libraries(mh_image_ilbm PUBLIC mh_image_decoding PRIVATE mh_assets)
mh_c89(mh_image_ilbm)

add_library(mh_image_fallback STATIC
	src/backends/image/fallback.c
	src/backends/image/effects.c)
target_link_libraries(mh_image_fallback
	PUBLIC
		mh_image_decoding
	PRIVATE
		mh_image_loading
		mh_demo_asset_layout
		mh_demo_presentation
		mh_image_tga
		mh_image_ilbm)
mh_c89(mh_image_fallback)

add_library(mh_image_pdi STATIC src/backends/image/pdi_source.c)
target_link_libraries(mh_image_pdi
	PUBLIC
		mh_image_collections
	PRIVATE
		mh_assets
		mh_resource_source)
mh_c89(mh_image_pdi)

add_library(mh_atlas_remap STATIC src/backends/image/remap_source.c)
target_link_libraries(mh_atlas_remap
	PUBLIC
		mh_atlas_map
	PRIVATE
		mh_assets
		mh_resource_source)
mh_c89(mh_atlas_remap)
