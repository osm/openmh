#ifndef MH_OPENAL_AUDIO_INTERNAL_H
#define MH_OPENAL_AUDIO_INTERNAL_H

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

#include "backends/audio/openal_provider.h"

#define MH_OPENAL_BUFFERS 64
#define MH_OPENAL_QUEUE_BYTES (16UL * 1024 * 1024)
#define MH_OPENAL_AHEAD_MS 75
#define MH_OPENAL_BLOCK_FRAMES 1024

struct mh_openal_device {
	struct mh_openal_device		*next;
	struct mh_openal_audio		*owner;
	struct mh_audio_callbacks	callbacks;
	ALuint				source, buffers[MH_OPENAL_BUFFERS];
	ALenum				format;
	size_t	lengths[MH_OPENAL_BUFFERS], frames, frame_bytes;
	float				block[MH_OPENAL_BLOCK_FRAMES * 2];
	size_t				block_bytes;
	int				batching;
	unsigned int			head, count, rate;
	int	start_attempted, started, paused, failed;
	unsigned long			service_calls;
	double				phase, next_service;
};

struct mh_openal_audio {
	struct mh_audio_provider	provider;
	ALCdevice			*output;
	ALCcontext			*context;
	struct mh_openal_device		*devices;
	int				interval, suspended;
};

extern const struct mh_audio_device_ops mh_openal_device_ops;
void			mh_openal_resume(void);
enum mh_error_code	mh_openal_status(struct mh_error *error_out);
enum mh_error_code	mh_openal_activate(struct mh_openal_audio *audio,
	struct mh_error *error_out);
void			mh_openal_pump(struct mh_openal_device *device);
void			mh_openal_play(struct mh_openal_device *device);
void			mh_openal_reap(struct mh_openal_device *device);
size_t			mh_openal_remaining(struct mh_openal_device *device);
enum mh_error_code	mh_openal_write(void *context, void *token,
	const void *bytes, size_t size, struct mh_error *error_out);

#endif
