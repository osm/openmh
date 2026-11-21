#ifndef MH_ASSET_PATHS_AUDIO_H
#define MH_ASSET_PATHS_AUDIO_H

#include "engine/assets/path_directories.h"

/* Sample names relative to Sounds. Material/ESD sample names stay in data. */
#define MH_ASSET_SAMPLE_GASON "gason.wav"
#define MH_ASSET_SAMPLE_GASRELE "gasrele.wav"
#define MH_ASSET_SAMPLE_GASOTL "gasotl.wav"
#define MH_ASSET_SAMPLE_GASOTR "gasotr.wav"
#define MH_ASSET_SAMPLE_GASRTL "gasrtl.wav"
#define MH_ASSET_SAMPLE_GASRTR "gasrtr.wav"
#define MH_ASSET_SAMPLE_INNEEN "inneen.wav"
#define MH_ASSET_SAMPLE_TURBO "turbo.wav"
#define MH_ASSET_SAMPLE_ENEMYENG "enemyeng.wav"
#define MH_ASSET_SAMPLE_GEAR3 "gear3.wav"
#define MH_ASSET_SAMPLE_FINISH2 "finish2.wav"
#define MH_ASSET_SAMPLE_ELIMINATED1 "eliminated1.wav"
#define MH_ASSET_SAMPLE_SLIDE1 "slide1.wav"
#define MH_ASSET_SAMPLE_SLIDEE1 "slidee1.wav"
#define MH_ASSET_SAMPLE_SLIDEE2 "slidee2.wav"
#define MH_ASSET_SAMPLE_SPIN "spin.wav"
#define MH_ASSET_SAMPLE_BARREL "barrel.wav"
#define MH_ASSET_SAMPLE_CRASH2 "crash2.wav"
#define MH_ASSET_SAMPLE_MAJCRASH "majcrash.wav"
#define MH_ASSET_SAMPLE_EBURST1 "eburst1.wav"
#define MH_ASSET_SAMPLE_EBURST2 "eburst2.wav"
#define MH_ASSET_SAMPLE_RULLGRU "rullgru.wav"
#define MH_ASSET_SAMPLE_ASFALT2 "asfalt2.wav"
#define MH_ASSET_SAMPLE_COUNTDOWN "starten/!32.wav"
#define MH_ASSET_SAMPLE_GO "starten/go!.wav"
#define MH_ASSET_SAMPLE_CHECKPOINT "starten/checkpoint.wav"
#define MH_ASSET_SAMPLE_MESSAGE "menu/cursor1.wav"

/* Paths relative to the installation root. */
#define MH_ASSET_SOUND_BARREL MH_ASSET_SOUNDS_PREFIX MH_ASSET_SAMPLE_BARREL
#define MH_ASSET_SOUND_CRASH2 MH_ASSET_SOUNDS_PREFIX MH_ASSET_SAMPLE_CRASH2
#define MH_ASSET_SOUND_MAJCRASH MH_ASSET_SOUNDS_PREFIX MH_ASSET_SAMPLE_MAJCRASH
#define MH_ASSET_PAUSE_LOOP MH_ASSET_SOUNDS_PREFIX "menyloop.wav"
#define MH_ASSET_PAUSE_ENTER MH_ASSET_SOUNDS_PREFIX "enter.wav"
#define MH_ASSET_PAUSE_CURSOR MH_ASSET_SOUNDS_PREFIX "cursor.wav"
#define MH_ASSET_PAUSE_ERROR MH_ASSET_SOUNDS_PREFIX "error.wav"
#define MH_ASSET_MENU_SOUND_CONFIG MH_ASSET_SOUNDS_PREFIX "Menu/menusound.cfg"
#define MH_ASSET_SOUND_FORMAT MH_ASSET_SOUNDS_PREFIX "%s"
#define MH_ASSET_BOUNDED_SOUND_FORMAT MH_ASSET_SOUNDS_PREFIX "%.*s"
#define MH_ASSET_MENU_SOUND_FORMAT MH_ASSET_SOUNDS_PREFIX "Menu/%s"
#define MH_ASSET_HORN_SUBDIR "CarHorns"
#define MH_ASSET_HORNS_DIR MH_ASSET_SOUNDS_PREFIX MH_ASSET_HORN_SUBDIR
#define MH_ASSET_HORN_FORMAT MH_ASSET_HORNS_DIR "/%s"

/* Filenames within the horn and music directories. */
#define MH_ASSET_SILENT_HORN_NAME "None.chf"
#define MH_ASSET_CD_TRACK_NAME_FORMAT "track-%02u.wav"

#endif
