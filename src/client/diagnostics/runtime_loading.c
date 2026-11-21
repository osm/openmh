#include <string.h>

#include "backends/animation/lightwave_provider.h"
#include "backends/audio/esd_provider.h"
#include "backends/audio/provider.h"
#include "backends/catalog/provider.h"
#include "backends/collision/provider.h"
#include "backends/font/provider.h"
#include "backends/geometry/lob_provider.h"
#include "backends/geometry/myo_provider.h"
#include "backends/geometry/myw_provider.h"
#include "backends/image/act_provider.h"
#include "backends/image/pdi_provider.h"
#include "backends/image/provider.h"
#include "backends/image/remap_provider.h"
#include "backends/image/tga_provider.h"
#include "backends/material/provider.h"
#include "backends/navigation/provider.h"
#include "backends/render/sdl_provider.h"
#include "backends/retro/provider.h"
#include "backends/sprite/spr_provider.h"
#include "backends/storage/filesystem_provider.h"
#include "backends/ui/dta_provider.h"
#include "client/diagnostics/runtime_game_clock.h"
#include "client/diagnostics/runtime_loading.h"
#include "client/hosts/storage.h"
#include "interfaces/render/service.h"

void
mh_diagnostic_loading_init(struct mh_diagnostic_loading *loading,
	const char *root, int no_records, const char *record_directory)
{
	memset(loading, 0, sizeof(*loading));

	loading->options.image_source = mh_image_pdi_source();
	loading->options.font_source = mh_font_fallback_source();
	loading->options.wireframe_source = mh_wireframe_lob_source();
	loading->options.sprite_source = mh_sprite_spr_source();

	loading->options.model_decoder = mh_model_myo_decoder();
	loading->options.world_decoder = mh_world_myw_decoder();
	loading->options.atlas_source = mh_atlas_remap_source();
	loading->options.animation_source = mh_animation_lightwave_source();
	loading->options.navigation_source = mh_navigation_mot_dat_source();
	loading->options.catalog_source = mh_catalog_fallback_source();
	loading->options.profile_source = mh_profile_fallback_source();
	loading->options.palette_source = mh_palette_act_source();
	loading->options.layout_source = mh_menu_layout_dta_source();
	loading->options.retro_source = mh_retro_fallback_source();
	loading->options.material_source = mh_material_mat_srf_source();
	loading->options.env_sound_source = mh_env_sounds_esd_source();
	loading->options.horn_source = mh_horn_fallback_source();
	loading->options.collision_source = mh_collision_fallback_source();
	loading->options.image_decoder = mh_image_fallback_decoder();
	loading->options.thumbnail_decoder = mh_image_tga_thumbnail_decoder();
	loading->options.portrait_decoder = mh_image_tga_portrait_decoder();
	loading->options.resources = mh_filesystem_resources();
	loading->options.user_data = mh_host_user_data();
	loading->options.windows = mh_sdl_render_service()->windows;
	loading->options.renderers = mh_sdl_render_service()->renderers;
	loading->options.timer = mh_game_clock_timer();
	loading->options.no_records = no_records;
	loading->options.record_directory = record_directory;
	mh_session_loading_init(&loading->screen, root, &loading->options);
}
