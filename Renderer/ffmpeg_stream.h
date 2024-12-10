#include <stdio.h>
#include <stdlib.h>
#include <type_traits>
#include <iostream>
#include <fstream>
extern "C" {
#include "libavformat/avformat.h"
#include "libavformat/avio.h"
#include "libavdevice/avdevice.h"
#include "libavcodec/avcodec.h"

#include "libavutil/audio_fifo.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/frame.h"
#include "libavutil/opt.h"

#include "libswresample/swresample.h"
#include <libswscale/swscale.h>
}


struct ffmpeg_packet;


struct video_params {
	const char* path = NULL;
	int time_size_second;
	double fps;
	int start;
	int end;
	int end_concat_time;
	int type_template;
};

struct StreamingParams {
	char copy_video;
	char copy_audio;
	const char* output_extension;
	const char* muxer_opt_key;
	const char* muxer_opt_value;
	const char* video_codec;
	const char* audio_codec;
	const char* codec_priv_key;
	const char* codec_priv_value;
	int index_stream;
};
enum stream_mod{AUDIO,VIDEO};
class ffmpeg_stream
{
private:
	int audio_stream_idx;
	int video_stream_idx;
	int size_per_second;
	AVFormatContext* format_context = NULL;
	AVCodecContext* audio_codec_context = NULL;
	AVCodecContext* video_codec_context = NULL;
	AVCodec* output_codec = NULL;
	AVCodec* video_codec = NULL;
	AVCodec* audio_codec = NULL;
	AVStream* audio_stream;
	AVStream* video_stream;
	SwrContext* resample_context = NULL;
	AVAudioFifo* fifo = NULL;
	StreamingParams streaminParams;
	int output_frame_size = 0;
	int encode_video_frame(ffmpeg_packet packet);
	
public:
	~ffmpeg_stream();
	enum read_frame_mode { VIDEO_FRAME, AUDIO_FRAME, AV_FRAME };
	enum open_mode { READ, WRITE };
	AVCodecContext* get_audio_codec_context();
	AVCodecContext* get_video_codec_context();

	AVFormatContext* get_format_context();
	int get_frame_stream_id();
	int open_stream(const char* path, open_mode mode, stream_mod format);
	int prepare_metadata(ffmpeg_stream* metaDataStream); //for output stream only
	int prepare_video_encoder(ffmpeg_stream* metaDataStream);
	int prepare_audio_encoder(ffmpeg_stream* metaDataStream);
	int p_read(read_frame_mode frame_mode, ffmpeg_packet* packet);
	int p_write_audio(ffmpeg_packet packet);
	int p_write_audio_1(ffmpeg_packet packet);
	int p_write_audio2(ffmpeg_packet packet);
	int p_write_video(ffmpeg_packet packet);
	int concate_video(ffmpeg_stream output,video_params* start, video_params* end);
	int get_time_size();
	double get_fps();
	int ffmpeg_stream_free();
	int close_stream();

};

struct ffmpeg_packet
{
	ffmpeg_stream* ptr_stream = NULL;
	AVPacket* packet = NULL;
	AVFrame* dec_frame = NULL;
	int finished = 0;
	int data_present = 0;
	int index = 0;
	void dump();
	void dump_free();
};