#pragma once

#include <stdio.h>
#include <inttypes.h>
#include <iostream>
#include <vector>
#include <string>

extern "C" {
#include "libavutil/channel_layout.h"
#include "libavutil/md5.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"

#include "libavfilter/avfilter.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"

#include <libavformat/avformat.h>
#include <libavfilter/avfiltergraph.h>

#include "libavformat/avio.h"

#include "libavutil/audio_fifo.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/frame.h"
#include "libavutil/opt.h"
}

#include "ffmpeg_stream.h"

#define INPUT_SAMPLERATE     44100
#define INPUT_FORMAT         AV_SAMPLE_FMT_S16
#define INPUT_CHANNEL_LAYOUT AV_CH_LAYOUT_STEREO

#define OUTPUT_BIT_RATE 44100
#define OUTPUT_CHANNELS 2
#define OUTPUT_SAMPLE_FORMAT AV_SAMPLE_FMT_S16

#define VOLUME_VAL 0.90
enum overlay_mod { NOMOD, START, MIDDLE, END };

class ffmpeg_filter
{
private:
	ffmpeg_stream* first_input_codec_context = NULL;
	std::vector<AVFilterContext*> src;
	AVFilterContext* inp = NULL;
	AVFilterContext* out = NULL;
	AVFilterGraph* filter_graph = NULL;
	int input_count = 0;
	int start_ov_time;
	int end_ov_time;

public:
	~ffmpeg_filter();
	int init_filter(ffmpeg_stream* input_streams, ...);
	int init_overlay_filter(ffmpeg_stream* input_streams, std::vector<video_params> overlay_duration, int duration_input);
	int init_overlay_filter1(ffmpeg_stream* input_streams, video_params overlay_duration, int duration_input, overlay_mod ov_mode);
	int prepare_output();
	int merge_audio(ffmpeg_packet* out_packet, ffmpeg_packet input_packet, ...);
	int overlay_video(ffmpeg_packet* out_packet, ffmpeg_packet input_packet);
	int get_frame_time_pos(ffmpeg_packet input_packet);
	int filter_free();
};

