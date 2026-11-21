#include <stdio.h>
#include <string.h>

#include "engine/assets/track.h"
#include "engine/catalog/halo_paths.h"
#include "engine/catalog/track.h"
#include "engine/core/error.h"
#include "engine/game/league_tracks.h"

static int failures;

#define CHECK(expression)                                                      \
	do {                                                                   \
		if (!(expression)) {                                           \
			fprintf(stderr, "line %d: %s\n", __LINE__,             \
				#expression);                                  \
			failures++;                                            \
		}                                                              \
	} while (0)

int
main(void)
{
	static const char valid[] =
		"// visual settings\r\nTrackName Example\r\n"
		"Halo lamps\\blue.iff\nHaLo lamps\\blue.iff // duplicate slot\n"
		"EnvMapName Data\\env_city.iff // first\r\n"
		"envmapname Data\\SECOND.IFF\n\tENVMAPNAME Data\\third.iff";
	static const char *invalid[] = {"",
		"EnvMapName\nEnvMapName a\nEnvMapName b",
		"EnvMapName a b\nEnvMapName c\nEnvMapName d",
		"EnvMapName a\nEnvMapName b",
		"EnvMapName a\nEnvMapName b\nEnvMapName c\nEnvMapName d"};

	static const char *invalid_settings[] = {"Location", "Length",
		"LengthUS", "Location a\nLocation b", "Length a\nLength b",
		"LengthUS a\nLengthUS b", "Halo", "Halo a b", "LineObjectName",
		"LineObjectName a b", "LineObjectName a\nLineObjectName b",
		"TrackMapName", "TrackMapName a b",
		"TrackMapName a\nTrackMapName b", "TrackMapInfo",
		"TrackMapInfo 1 2 3", "TrackMapInfo 1 2 3 4 extra",
		"TrackMapInfo 1+2 3 4", "TrackMapInfo nan 2 3 4",
		"TrackMapInfo 1 2 inf 4", "TrackMapInfo 1 2 3 1e99",
		"TrackMapInfo 1 2 3 1e-999",
		"TrackMapInfo 1 2 3 4\nTrackMapInfo 1 2 3 4", "CueColour",
		"CueColour 1 2", "CueColour 1 2 3 extra", "CueColour 1+2 3",
		"CueColour nan 2 3", "CueColour 1 inf 3", "CueColour 1 2 1e99",
		"CueColour 0 0 0\nCUECOLOUR 1 2 3", "CueStart", "CueStart nan",
		"CueStart 1e99", "CueStart 0.2 extra",
		"CueStart 0\nCueStart 0.2", "GoalCamera",
		"GoalCamera 1 2 3 4 5", "GoalCamera 1 2 3 4 5 6 extra",
		"GoalCamera nan 2 3 4 5 6", "GoalCamera 1 2 inf 4 5 6",
		"GoalCamera 1e99 2 3 4 5 6", "GoalCamera 1+2 3 4 5 6",
		"GoalCamera 1 2 3 4 5 6\nGoalCamera 1 2 3 4 5 6", "AIGoalWheel",
		"AIGoalWheel nan", "AIGoalWheel inf", "AIGoalWheel 1e99",
		"AIGoalWheel 1e-999", "AIGoalWheel .3 extra", "AIGoalWheel 1+2",
		"AIGoalWheel .3\nAIGoalWheel .4", "Division", "Division -1",
		"Division 1.5", "Division 4294967296", "Division 1 extra",
		"Division 0\nDivision 1", "WorldBrightness 1 2",
		"WorldBrightness 1 2 3 extra", "WorldBrightness -1 2 3",
		"WorldBrightness nan 1 1", "WorldBrightness 1e999 1 1",
		"WorldBrightness 1 1 101", "WorldBrightness 1+2+3",
		"WorldBrightness 1 1 1e-999",
		"WorldBrightness 1 1 1\nWorldBrightness 1 1 1",
		"3DAccBrightness 1 2", "3DAccBrightness 1 2 3 extra",
		"3DAccBrightness nan 1 1", "3DAccBrightness -1 1 1",
		"3DAccBrightness 1 101 1", "3DAccBrightness 1+2+3",
		"3DAccBrightness 1 1 1e-999",
		"3DAccBrightness 1 1 1\n3daccbrightness 1 1 1",
		"ObjectBrightness 1 2", "ObjectAmbient 1 2 3 extra",
		"ObjectAmbient nan 1 1", "LightIntensity nan",
		"LightIntensity -1", "LightIntensity 1 2", "LightIntensity",
		"SpecularFactor", "SpecularFactor nan", "SpecularFactor inf",
		"SpecularFactor -1", "SpecularFactor 101", "SpecularFactor 2 3",
		"SpecularFactor 1e-999", "SpecularFactor 2\nspecularfactor 6",
		"LightIntensity 101", "LightIntensity 1\nLIGHTINTENSITY 2",
		"ObjectBrightness 1 1 1\nObjectBrightness 1 1 1",
		"ObjectAmbient 1 1 1\nObjectAmbient 1 1 1", "BackgroundName",
		"BackgroundName a b", "BackgroundName a\nBackgroundName b",
		"TopColour 1 2", "TopColour 1 2 256", "TopColour 1 nan 2",
		"TopColour 1 2 3 extra", "TopColour 1 2 3\nTopColour 4 5 6",
		"BottomColour 1 -1 2", "BottomColour 1 2 1.5",
		"BottomColour 1 2 3\nBottomColour 4 5 6", "Horizon nan",
		"Horizon -1", "Horizon 1.1", "Horizon .2 extra",
		"Horizon .2\nHorizon .3", "ZeroTransparent",
		"ZeroTransparent tree.iff extra", "BasePath", "FileName a b",
		"BasePath a\nBasePath b", "SceneName",
		"SceneName a\nSceneName b", "SceneName a b", "CameraSplineName",
		"CameraSplineName a b",
		"CameraSplineName a\nCameraSplineName b", "StartSpline1",
		"StartSpline1 a b", "StartSpline1 a\nStartSpline1 b",
		"StartSpline2", "StartSpline2 a b",
		"StartSpline2 a\nStartSpline2 b", "CollisionName",
		"CollisionName a\nCollisionName b", "MaterialsName a b",
		"SplineName a\nSplineName b", "StartGrid 1 2 3",
		"StartGrid 1 2 3 4 extra", "StartGrid 1 2 -1 4",
		"StartGrid 1 2 nan 4", "StartGrid 1 2 3 101",
		"StartGrid 1 2 3 4\nStartGrid 1 2 3 4", "Laps 0", "Laps 26",
		"Laps -1", "Laps nan", "Laps 1.5", "Laps 3 extra",
		"Laps 3\nLAPS 5", "AITrackName a b",
		"AITrackName a\nAITrackName b", "CDTrack -1", "CDTrack 1",
		"CDTrack 100", "CDTrack nan", "CDTrack 2.5", "CDTrack 4 extra",
		"CDTrack 0\nCDTrack 4", "CDTrack", "ViewDistance",
		"ViewDistance 0", "ViewDistance -1", "ViewDistance nan",
		"ViewDistance 1e999", "ViewDistance 1e-999",
		"ViewDistance 1e-100", "ViewDistance 1000001",
		"ViewDistance 180 extra", "ViewDistance 180\nVIEWDISTANCE 120",
		"TrackName", "TrackName duplicate", "TrackName two words",
		"EnvSoundName", "EnvSoundName a b",
		"EnvSoundName a\nEnvSoundName b", "AI1 1 2 3",
		"AI1 nan 0 10 0 10 0 0 0 2 14 15",
		"AI2 7 0 10 0 10 0 0 0 2 14 101",
		"AI3 7 0 10 0 10 0 0 0 2 14 15 extra",
		"AI1 7 0 10 0 10 0 0 0 2 14 15\nAI1 7 0 10 0 10 0 0 0 2 14 15"};

	struct mh_track track;
	struct mh_track saved;
	char text[2048];
	size_t length;
	unsigned int i;

	CHECK(mh_track_config_trk_decode((const unsigned char *)valid,
		sizeof(valid) - 1, &track, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(track.environment[0], "Data\\env_city.iff") == 0);
	CHECK(strcmp(track.name, "Example") == 0);
	CHECK(strcmp(track.environment[1], "Data\\SECOND.IFF") == 0);
	CHECK(strcmp(track.environment[2], "Data\\third.iff") == 0);
	CHECK(track.halos.count == 2);
	CHECK(strcmp(mh_halo_path(&track.halos, 0), "lamps\\blue.iff") == 0);
	CHECK(strcmp(mh_halo_path(&track.halos, 1), "lamps\\blue.iff") == 0);
	CHECK(mh_halo_path(&track.halos, 2) == NULL);
	CHECK(track.line_object[0] == 0);
	CHECK(track.base_path[0] == 0 && track.world_file[0] == 0 &&
		track.scene_file[0] == 0);
	CHECK(track.world_brightness[0] == 1.0 &&
		track.world_brightness[2] == 1.0);
	CHECK(track.object_brightness[0] == 1.0 &&
		track.object_ambient[0] == 0.0);
	CHECK(track.light_intensity == 1.0);
	CHECK(track.specular_factor == 1.0);
	CHECK(track.texture_brightness[0] == 1 &&
		track.texture_brightness[1] == 1 &&
		track.texture_brightness[2] == 1);
	CHECK(!track.has_division);
	CHECK(!track.has_goal_camera);
	CHECK(!track.has_ai_goal_wheel && track.ai_goal_wheel == 0);
	CHECK(!track.has_cue_color && !track.has_cue_start);
	CHECK(track.sky_settings == 0 && track.background[0] == 0);
	CHECK(!track.has_map_info && track.map_name[0] == 0);
	CHECK(track.location[0] == 0 && track.length[0] == 0 &&
		track.length_us[0] == 0);
	strcpy(text, valid);
	strcat(text,
		"\nLocation Arizona,\tUSA\nLength 4.7 km // authored\nLengthUS 2.9 miles ignored");
	CHECK(mh_track_config_trk_decode((const unsigned char *)text,
		strlen(text), &track, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(track.location, "Arizona, USA") == 0);
	CHECK(strcmp(track.length, "4.7 km") == 0);
	CHECK(strcmp(track.length_us, "2.9 miles") == 0);
	strcpy(text, valid);
	strcat(text,
		"\nLocation Earth\nLength unknown\nLengthUS unknown /note");
	CHECK(mh_track_config_trk_decode((const unsigned char *)text,
		strlen(text), &track, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(track.location, "Earth") == 0);
	CHECK(strcmp(track.length, "unknown") == 0);
	CHECK(strcmp(track.length_us, "unknown") == 0);
	saved = track;
	strcpy(text, valid);
	strcat(text, "\nLength ");
	length = strlen(text);
	memset(text + length, 'x', MH_TRACK_TEXT_SIZE);
	text[length + MH_TRACK_TEXT_SIZE] = 0;
	CHECK(mh_track_config_trk_decode((const unsigned char *)text,
		strlen(text), &track, NULL) == MH_ERROR_RANGE);
	CHECK(memcmp(&track, &saved, sizeof(track)) == 0);
	strcpy(text, valid);
	strcat(text, "\nLineObjectName Tracks\\Track1\\Objects\\redrock.lob");
	CHECK(mh_track_config_trk_decode((const unsigned char *)text,
		strlen(text), &track, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(track.line_object,
		"Tracks\\Track1\\Objects\\redrock.lob") == 0);
	strcpy(text, valid);
	strcat(text,
		"\nTrackMapName Tracks\\Track3\\Data\\okkcity\nTrackMapInfo -289.27 2787.5 -150.6904 50");
	CHECK(mh_track_config_trk_decode((const unsigned char *)text,
		strlen(text), &track, NULL) == MH_ERROR_NONE);
	CHECK(track.has_map_info && track.map_info[0] == -289.27f &&
		track.map_info[1] == 2787.5f &&
		track.map_info[2] == -150.6904f && track.map_info[3] == 50);
	CHECK(strcmp(track.map_name, "Tracks\\Track3\\Data\\okkcity") == 0);
	CHECK(track.zero_transparent_size == 0 &&
		!mh_track_zero_transparent(&track, "tree.iff"));
	strcpy(text, valid);
	strcat(text,
		"\nZeroTransparent Tree.iff\nZEROTRANSPARENT fence.iff\nZeroTransparent Tree.iff // duplicates are harmless");
	CHECK(mh_track_config_trk_decode((const unsigned char *)text,
		strlen(text), &track, NULL) == MH_ERROR_NONE);
	CHECK(mh_track_zero_transparent(&track, "TREE.IFF"));
	CHECK(mh_track_zero_transparent(&track, "fence.iff") &&
		!mh_track_zero_transparent(&track, "road.iff"));
	CHECK(mh_track_zero_transparent(&track, "tree") &&
		!mh_track_zero_transparent(&track, ""));
	CHECK(!mh_track_zero_transparent(NULL, "tree.iff") &&
		!mh_track_zero_transparent(&track, NULL));
	saved = track;
	strcpy(text, valid);

	for (i = 0; i < 5; i++) {
		strcat(text, "\nZeroTransparent ");
		length = strlen(text);
		memset(text + length, 'a', 250);
		text[length + 250] = 0;
	}

	CHECK(mh_track_config_trk_decode((const unsigned char *)text,
		strlen(text), &track, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &track, sizeof(track)) == 0);
	strcpy(text, valid);
	strcat(text,
		"\nBackgroundName sky.tga\nTopColour 71 13 14\nBottomColour 129 38 15\nHorizon .24");
	CHECK(mh_track_config_trk_decode((const unsigned char *)text,
		strlen(text), &track, NULL) == MH_ERROR_NONE);
	CHECK(track.sky_settings == 7 && track.horizon == (double)(float).24);
	CHECK(strcmp(track.background, "sky.tga") == 0 &&
		track.sky_top[0] == 71 && track.sky_bottom[2] == 15);
	strcpy(text, valid);
	strcat(text, "\nCueColour 130.5 39 16\nCueStart 0.22");
	CHECK(mh_track_config_trk_decode((const unsigned char *)text,
		strlen(text), &track, NULL) == MH_ERROR_NONE);
	CHECK(track.has_cue_color && track.cue_color[0] == 130.5 &&
		track.cue_color[2] == 16);
	CHECK(track.has_cue_start && track.cue_start == (double)(float).22);
	CHECK(track.cd_track == 0 && track.view_distance == 0 &&
		track.camera_file[0] == 0);
	strcpy(text, valid);
	strcat(text,
		"\nBasePath Tracks\\Track1\nFileName redrock.myw\nWorldBrightness 0.89 1.19 1.34");
	strcat(text, "\nSceneName Tracks\\Track1\\redrock_move.lws");
	strcat(text,
		"\nCollisionName track.col\nMaterialsName track.srf\nSplineName track.mot");
	strcat(text,
		"\nObjectBrightness 0.5 0.6 0.7\nobjectambient 0.1 0.2 0.3\nLIGHTINTENSITY 2 // comment");
	strcat(text, "\n3DAccBrightness 1.6 1.7 1.4\nSpecularFactor 6.1");
	CHECK(mh_track_config_trk_decode((const unsigned char *)text,
		strlen(text), &track, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(track.base_path, "Tracks\\Track1") == 0);
	CHECK(strcmp(track.world_file, "redrock.myw") == 0);
	CHECK(strcmp(track.scene_file, "Tracks\\Track1\\redrock_move.lws") ==
		0);
	CHECK(strcmp(track.collision_file, "track.col") == 0);
	CHECK(strcmp(track.materials_file, "track.srf") == 0);
	CHECK(strcmp(track.spline_file, "track.mot") == 0);
	CHECK(track.world_brightness[0] == 0.89 &&
		track.world_brightness[2] == 1.34);
	CHECK(track.object_brightness[2] == 0.7 &&
		track.object_ambient[1] == 0.2);
	CHECK(track.light_intensity == 2.0);
	CHECK(track.specular_factor == (double)6.1f);
	CHECK(track.texture_brightness[0] == (double)1.6f &&
		track.texture_brightness[1] == (double)1.7f &&
		track.texture_brightness[2] == (double)1.4f);
	CHECK(!track.has_start_grid && track.laps == 0);
	strcat(text,
		"\nStartGrid 12 6 3 5.5\nLaps 3\nCDTrack 4\nEnvSoundName track1/sounds.esd");
	strcat(text, "\nViewDistance 180.1 // original float setting");
	strcat(text,
		"\nCameraSplineName Tracks\\Track1\\Motions\\wastecam.mot");
	CHECK(mh_track_config_trk_decode((const unsigned char *)text,
		strlen(text), &track, NULL) == MH_ERROR_NONE);
	CHECK(track.has_start_grid && track.start_grid[0] == 12 &&
		track.start_grid[3] == 5.5);
	CHECK(track.laps == 3);
	CHECK(track.cd_track == 4);
	CHECK(track.view_distance == (double)(float)180.1);
	CHECK(strcmp(track.camera_file,
		"Tracks\\Track1\\Motions\\wastecam.mot") == 0);
	CHECK(strcmp(track.env_sound_file, "track1/sounds.esd") == 0);
	strcat(text,
		"\nAITrackName Tracks\\Track1\nAI1 7 0 10 .1 10 .1 .2 .1 2.3 14 15");
	CHECK(mh_track_config_trk_decode((const unsigned char *)text,
		strlen(text), &track, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(track.ai_path, "Tracks\\Track1") == 0 && track.has_ai[0] &&
		!track.has_ai[1]);
	CHECK(track.ai[0][8] == (double)(float)2.3 && track.ai[0][9] == 14);
	strcat(text,
		"\nGoalCamera -1200.1 25 4000 -1199 0 3990 // eye and target");
	CHECK(mh_track_config_trk_decode((const unsigned char *)text,
		strlen(text), &track, NULL) == MH_ERROR_NONE);
	CHECK(track.has_goal_camera &&
		track.goal_camera[0] == (double)(float)-1200.1);
	CHECK(track.goal_camera[2] == 4000 && track.goal_camera[5] == 3990);
	strcat(text, "\naigoalwheel -.3 // finished AI steering");
	CHECK(mh_track_config_trk_decode((const unsigned char *)text,
		strlen(text), &track, NULL) == MH_ERROR_NONE);
	CHECK(track.has_ai_goal_wheel && track.ai_goal_wheel == -.3f);
	strcat(text, "\nDivision 101");
	CHECK(mh_track_config_trk_decode((const unsigned char *)text,
		strlen(text), &track, NULL) == MH_ERROR_NONE);
	CHECK(track.has_division && track.division == 101);
	CHECK(mh_league_track_eligible(101, 3, 0));
	CHECK(!mh_league_track_eligible(200, 3, 0));
	CHECK(mh_league_track_eligible(200, 2, 0));
	CHECK(mh_league_track_eligible(0, 2, 0));
	CHECK(!mh_league_track_eligible(310, 1, 0));
	CHECK(mh_league_track_eligible(310, 1, 1));
	CHECK(mh_league_track_eligible(400, 0, 0));
	CHECK(!mh_league_track_eligible(301, 0, 0));
	CHECK(!mh_league_track_eligible(101, 4, 0));
	saved = track;

	for (i = 0; i < sizeof(invalid_settings) / sizeof(invalid_settings[0]);
		i++) {
		strcpy(text, valid);
		strcat(text, "\n");
		strcat(text, invalid_settings[i]);
		CHECK(mh_track_config_trk_decode((const unsigned char *)text,
			strlen(text), &track, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&track, &saved, sizeof(track)) == 0);
	}

	for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		CHECK(mh_track_config_trk_decode(
			(const unsigned char *)invalid[i],
			strlen(invalid[i]), &track,
			NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&track, &saved, sizeof(track)) == 0);
	}

	strcpy(text, "EnvMapName ");
	memset(text + 11, 'a', 255);
	strcpy(text + 266, "\nEnvMapName b\nEnvMapName c");
	length = strlen(text);
	CHECK(mh_track_config_trk_decode((const unsigned char *)text, length,
		&track, NULL) == MH_ERROR_NONE);
	CHECK(strlen(track.environment[0]) == 255);
	saved = track;
	text[266] = 'a'; /* A 256-byte path must not be silently truncated. */
	CHECK(mh_track_config_trk_decode((const unsigned char *)text, length,
		&track, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&track, &saved, sizeof(track)) == 0);
	strcpy(text, valid);
	text[0] = 0;
	CHECK(mh_track_config_trk_decode((const unsigned char *)text,
		sizeof(valid) - 1, &track, NULL) != MH_ERROR_NONE);
	text[0] = (char)255;
	CHECK(mh_track_config_trk_decode((const unsigned char *)text,
		sizeof(valid) - 1, &track, NULL) != MH_ERROR_NONE);
	memset(text, 'a', sizeof(text));
	CHECK(mh_track_config_trk_decode((const unsigned char *)text,
		sizeof(text), &track, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&track, &saved, sizeof(track)) == 0);

	if (failures != 0)
		return 1;

	puts("Track environment selection and malformed-input checks passed");

	return 0;
}
