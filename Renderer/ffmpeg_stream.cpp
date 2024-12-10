#include "ffmpeg_stream.h"

#define OUTPUT_BIT_RATE 48000
#define OUTPUT_CHANNELS 2

SwsContext* swsCtx = NULL;
AVFrame* videoFrame = NULL;
int frameCounter = 0;
static void pushFrame(uint8_t* data, AVCodecContext* cctx, AVFormatContext* ofctx, int* pts, int* dts) {
	int err;
	if (!videoFrame) {
		videoFrame = av_frame_alloc();
		videoFrame->format = AV_PIX_FMT_YUV420P;
		videoFrame->width = cctx->width;
		videoFrame->height = cctx->height;
		if ((err = av_frame_get_buffer(videoFrame, 32)) < 0) {
			std::cout << "Failed to allocate picture" << err << std::endl;
			return;
		}
	}
	if (!swsCtx) {
		swsCtx = sws_getContext(cctx->width, cctx->height, AV_PIX_FMT_RGB24, cctx->width,
			cctx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, 0, 0, 0);
	}
	int inLinesize[1] = { 3 * cctx->width };
	// From RGB to YUV
	sws_scale(swsCtx, (const uint8_t* const*)&data, inLinesize, 0, cctx->height,
		videoFrame->data, videoFrame->linesize);
	videoFrame->pts = (1.0 / 30.0) * 90000 * (frameCounter++);
	std::cout << videoFrame->pts << " " << cctx->time_base.num << " " <<
		cctx->time_base.den << " " << frameCounter << std::endl;
	if ((err = avcodec_send_frame(cctx, videoFrame)) < 0) {
		std::cout << "Failed to send frame" << err << std::endl;
		return;
	}
	AV_TIME_BASE;
	AVPacket pkt;
	av_init_packet(&pkt);
	pkt.data = NULL;
	pkt.size = 0;
	pkt.flags |= AV_PKT_FLAG_KEY;
	if (avcodec_receive_packet(cctx, &pkt) == 0) {
		static int counter = 0;
	
		std::cout << "pkt key: " << (pkt.flags & AV_PKT_FLAG_KEY) << " " <<
			pkt.size << " " << (counter++) << std::endl;
		uint8_t* size = ((uint8_t*)pkt.data);
		std::cout << "first: " << (int)size[0] << " " << (int)size[1] <<
			" " << (int)size[2] << " " << (int)size[3] << " " << (int)size[4] <<
			" " << (int)size[5] << " " << (int)size[6] << " " << (int)size[7] <<
			std::endl;
		pkt.pts += *pts;
		pkt.dts += *dts;
		av_interleaved_write_frame(ofctx, &pkt);

		av_packet_unref(&pkt);
	}
}
int prepare_copy(AVFormatContext* avfc, AVStream** avs, AVCodecParameters* decoder_par) {
	*avs = avformat_new_stream(avfc, NULL);
	avcodec_parameters_copy((*avs)->codecpar, decoder_par);
	return 0;
}

int remux(AVPacket** pkt, AVFormatContext** avfc, AVRational decoder_tb, AVRational encoder_tb) {

	av_packet_rescale_ts(*pkt, decoder_tb, encoder_tb);

	if (av_interleaved_write_frame(*avfc, *pkt) < 0) { printf("error while copying stream packet"); return -1; }
	return 0;
}

int fill_stream_info(AVStream* avs, AVCodec** avc, AVCodecContext** avcc) {
	*avc = avcodec_find_decoder(avs->codecpar->codec_id);
	if (!*avc) { printf("failed to find the codec"); return -1; }

	*avcc = avcodec_alloc_context3(*avc);
	if (!*avcc) { printf("failed to alloc memory for codec context"); return -1; }

	if (avcodec_parameters_to_context(*avcc, avs->codecpar) < 0) { printf("failed to fill codec context"); return -1; }

	if (avcodec_open2(*avcc, *avc, NULL) < 0) { printf("failed to open codec"); return -1; }
	return 0;
}

int open_input_file(const char* filename,
	AVFormatContext** input_format_context,
	AVCodecContext** input_audio_codec_context,
	AVCodecContext** input_video_codec_context,
	AVCodec** video_codec,
	AVCodec** audio_codec,
	AVStream** video_stream,
	AVStream** audio_stream,
	int& audio_stream_idx, int& video_stream_idx, stream_mod format)
{
	AVCodec* input_codec;
	int error;
	*input_format_context = avformat_alloc_context();
	if ((error = avformat_open_input(input_format_context, filename, NULL, NULL)) < 0) {
		printf("Could not open input file)\n");
		*input_format_context = NULL;
		return error;
	}
	if ((error = avformat_find_stream_info(*input_format_context, NULL)) < 0) {
		printf("Could not open find stream info (error)\n");
		avformat_close_input(input_format_context);
		return error;
	}
	if (format == VIDEO) {
		for (int i = 0; i < (*input_format_context)->nb_streams; i++) {
			if ((*input_format_context)->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
				*video_stream = (*input_format_context)->streams[i];
				video_stream_idx = i;

				if (fill_stream_info(*video_stream, video_codec, input_video_codec_context)) { return -1; }
			}
			if ((*input_format_context)->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
				audio_stream_idx = i;
				*audio_stream = (*input_format_context)->streams[i];

				if (fill_stream_info(*audio_stream, audio_codec, input_audio_codec_context)) { return -1; }

			}
		}
	}
	else {
		if (audio_stream_idx >= (*input_format_context)->nb_streams) {
			fprintf(stderr, "Could not find an audio stream\n");
			avformat_close_input(input_format_context);
			return AVERROR_EXIT;
		}

		if (!(*video_codec = avcodec_find_decoder((*input_format_context)->streams[audio_stream_idx]->codecpar->codec_id))) {
			fprintf(stderr, "Could not find input codec\n");
			avformat_close_input(input_format_context);
			return AVERROR_EXIT;
		}

		*input_audio_codec_context = avcodec_alloc_context3(*video_codec);
		if (!*input_audio_codec_context) {
			fprintf(stderr, "Could not allocate a decoding context\n");
			avformat_close_input(input_format_context);
			return AVERROR(ENOMEM);
		}

		error = avcodec_parameters_to_context(*input_audio_codec_context, (*input_format_context)->streams[audio_stream_idx]->codecpar);
		if (error < 0) {
			avformat_close_input(input_format_context);
			avcodec_free_context(input_audio_codec_context);
			return error;
		}

		if ((error = avcodec_open2(*input_audio_codec_context, *video_codec, NULL)) < 0) {
			fprintf(stderr, "Could not open input codec (error)");
			avcodec_free_context(input_audio_codec_context);
			avformat_close_input(input_format_context);
			return error;
		}



	}

	return 0;
}

static int check_sample_fmt(const AVCodec* codec, enum AVSampleFormat sample_fmt)
{
	const enum AVSampleFormat* p = codec->sample_fmts;
	while (*p != AV_SAMPLE_FMT_NONE) {
		if (*p == sample_fmt)
			return 1;
		p++;
	}
	return 0;
}
static int open_output_file(const char* filename,
	AVFormatContext** output_format_context,
	AVCodecContext** output_codec_context,
	AVCodec** output_codec, StreamingParams* sp)
{
	avformat_alloc_output_context2(output_format_context, NULL, NULL, filename);


	int error = 0;
cleanup:


	return 0;
}

static void init_packet(AVPacket* packet)
{
	av_init_packet(packet);
	packet->data = NULL;
	packet->size = 0;
}

static int init_input_frame(AVFrame** frame)
{
	if (!(*frame = av_frame_alloc())) {
		fprintf(stderr, "Could not allocate input frame\n");
		return AVERROR(ENOMEM);
	}
	return 0;
}

static int init_resampler(AVCodecContext* input_codec_context,
	AVCodecContext* output_codec_context,
	SwrContext** resample_context)
{
	int error;

	*resample_context = swr_alloc_set_opts(NULL,
		av_get_default_channel_layout(output_codec_context->channels),
		output_codec_context->sample_fmt,
		output_codec_context->sample_rate,
		av_get_default_channel_layout(input_codec_context->channels),
		input_codec_context->sample_fmt,
		input_codec_context->sample_rate,
		0, NULL);
	if (!*resample_context) {
		fprintf(stderr, "Could not allocate resample context\n");
		return AVERROR(ENOMEM);
	}
	av_assert0(output_codec_context->sample_rate == input_codec_context->sample_rate);

	if ((error = swr_init(*resample_context)) < 0) {
		fprintf(stderr, "Could not open resample context\n");
		swr_free(resample_context);
		return error;
	}
	return 0;
}

static int init_fifo(AVAudioFifo** fifo, AVCodecContext* output_codec_context)
{
	if (!(*fifo = av_audio_fifo_alloc(output_codec_context->sample_fmt,
		output_codec_context->channels, 1))) {
		fprintf(stderr, "Could not allocate FIFO\n");
		return AVERROR(ENOMEM);
	}
	return 0;
}

static int write_output_file_header(AVFormatContext* output_format_context)
{
	int error;
	if ((error = avformat_write_header(output_format_context, NULL)) < 0) {
		fprintf(stderr, "Could not write output file header (error)\n");
		return error;
	}
	return 0;
}
static int decode_audio_frame(AVFrame* frame,
	AVFormatContext* input_format_context,
	AVCodecContext* input_codec_context,
	int* data_present, int* finished, int* audio_stream_idx)
{
	AVPacket input_packet;
	int error;
	init_packet(&input_packet);

	if ((error = av_read_frame(input_format_context, &input_packet)) < 0) {
		if (error == AVERROR_EOF)
			*finished = 1;
		else {
			fprintf(stderr, "Could not read frame (error)\n");
			return error;
		}
	}
	if (error != AVERROR_EOF && input_packet.stream_index != *audio_stream_idx) goto cleanup;
	if ((error = avcodec_send_packet(input_codec_context, &input_packet)) < 0) {
		fprintf(stderr, "Could not send packet for decoding (error)\n");
		return error;
	}

	error = avcodec_receive_frame(input_codec_context, frame);
	if (error == AVERROR(EAGAIN)) {
		error = 0;
		goto cleanup;
	}
	else if (error == AVERROR_EOF) {
		*finished = 1;
		error = 0;
		goto cleanup;
	}
	else if (error < 0) {
		fprintf(stderr, "Could not decode frame (error)\n");
		goto cleanup;
	}
	else {
		*data_present = 1;
		goto cleanup;
	}

cleanup:
	av_packet_unref(&input_packet);
	return error;
}

static int decode_video_frame(AVFrame* frame, AVPacket* packet,
	AVFormatContext* input_format_context,
	AVCodecContext* input_video_codec_context, AVCodecContext* input_audio_codec_context,
	int* data_present, int* finished, int* video_stream_idx, int* audio_stream_idx, StreamingParams* sp) {
	AVPacket input_packet;
	int error = 0;
	init_packet(&input_packet);
	*data_present = 0;
	if ((error = av_read_frame(input_format_context, &input_packet)) < 0) {
		if (error == AVERROR_EOF)
			*finished = 1;
		else {
			fprintf(stderr, "Could not read frame (error)\n");
			return error;
		}
	}
	if (error != AVERROR_EOF && input_packet.stream_index == *audio_stream_idx && sp->copy_audio == 0) {
		int error = avcodec_send_packet(input_audio_codec_context, &input_packet);
		if (error < 0) { printf("Error while sending packet to decoder: %s"); return error; }

		error = avcodec_receive_frame(input_audio_codec_context, frame);
		if (error == AVERROR(EAGAIN) || error == AVERROR_EOF) {
			return error;
		}
		else if (error < 0) {
			printf("Error while receiving frame from decoder: %s");
			return error;
		}
		*data_present = 1;
		sp->index_stream = *audio_stream_idx;
		return error;
	}
	if (error != AVERROR_EOF && input_packet.stream_index == *audio_stream_idx) {
		*packet = input_packet;
		return error;
	}

	if (error != AVERROR_EOF && input_packet.stream_index != *video_stream_idx) { av_packet_unref(&input_packet); return error; }
	error = avcodec_send_packet(input_video_codec_context, &input_packet);
	if (error < 0) { printf("Error while sending packet to decoder: %s"); return error; }
	error = avcodec_receive_frame(input_video_codec_context, frame);
	if (error == AVERROR(EAGAIN) || error == AVERROR_EOF) {
		data_present = 0;
		return error;
	}
	else if (error < 0) {
		printf("Error while receiving frame from decoder: %s");
		return error;
	}
	else {
		*data_present = 1;
	}
	sp->index_stream = *video_stream_idx;

	frame->pts = frame->best_effort_timestamp;
	

	return error;
}

static int init_converted_samples(uint8_t*** converted_input_samples, AVCodecContext* output_codec_context, int frame_size)
{
	int error;

	if (!(*converted_input_samples = (uint8_t**)calloc(output_codec_context->channels, sizeof(**converted_input_samples)))) {
		fprintf(stderr, "Could not allocate converted input sample pointers \n");
		return AVERROR(ENOMEM);
	}


	if ((error = av_samples_alloc(*converted_input_samples, NULL, output_codec_context->channels, frame_size, output_codec_context->sample_fmt, 0) < 0)) {
		fprintf(stderr, "Could not allocate converted input samples (error)\n");
		av_freep(&(*converted_input_samples)[0]);
		free(*converted_input_samples);
		return error;
	}
	return 0;
}

static int convert_samples(const uint8_t** input_data,
	uint8_t** converted_data, const int frame_size,
	SwrContext* resample_context)
{
	int error;

	if ((error = swr_convert(resample_context, converted_data, frame_size, input_data, frame_size)) < 0) {
		fprintf(stderr, "Could not convert input samples (error)\n");
		return error;
	}

	return 0;
}

static int add_samples_to_fifo(AVAudioFifo* fifo,
	uint8_t** converted_input_samples,
	const int frame_size)
{
	int error;

	if ((error = av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) + frame_size)) < 0) {
		fprintf(stderr, "Could not reallocate FIFO\n");
		return error;
	}

	if (av_audio_fifo_write(fifo, (void**)converted_input_samples,
		frame_size) < frame_size) {
		fprintf(stderr, "Could not write data to FIFO\n");
		return AVERROR_EXIT;
	}
	return 0;
}



static int read_decode_convert_and_store(AVAudioFifo* fifo,
	AVFormatContext* input_format_context,
	AVCodecContext* input_codec_context,
	AVCodecContext* output_codec_context,
	SwrContext* resampler_context,
	int* audio_stream_idx,
	int* finished)
{
	AVFrame* input_frame = NULL;
	uint8_t** converted_input_samples = NULL;
	int data_present = 0;
	int ret = AVERROR_EXIT;

	if (init_input_frame(&input_frame))
		goto cleanup;
	if (decode_audio_frame(input_frame, input_format_context,
		input_codec_context, &data_present, finished, audio_stream_idx))
		goto cleanup;
	if (*finished) {
		ret = 0;
		goto cleanup;
	}
	if (data_present) {
		if (init_converted_samples(&converted_input_samples, output_codec_context,
			input_frame->nb_samples))
			goto cleanup;

		if (convert_samples((const uint8_t**)input_frame->extended_data, converted_input_samples,
			input_frame->nb_samples, resampler_context))
			goto cleanup;

		if (add_samples_to_fifo(fifo, converted_input_samples,
			input_frame->nb_samples))
			goto cleanup;
		ret = 0;
	}
	ret = 0;

cleanup:
	if (converted_input_samples) {
		av_freep(&converted_input_samples[0]);
		free(converted_input_samples);
	}
	av_frame_free(&input_frame);

	return ret;
}
int ffmpeg_stream::prepare_audio_encoder(ffmpeg_stream* metaDataStream) {
	this->audio_stream = avformat_new_stream(this->format_context, NULL);

	this->audio_codec = avcodec_find_encoder_by_name(this->streaminParams.audio_codec);
	if (!this->audio_codec) { printf("could not find the proper codec"); return -1; }

	this->audio_codec_context = avcodec_alloc_context3(this->audio_codec);
	if (!this->audio_codec_context) { printf("could not allocated memory for codec context"); return -1; }

	int OUTPUT_BIT_RATE1 = metaDataStream->audio_codec_context->bit_rate;
	AVRational av_rate;
	av_rate.den = 1;
	av_rate.num = metaDataStream->audio_codec_context->sample_rate;
	this->audio_codec_context->channels = OUTPUT_CHANNELS;
	this->audio_codec_context->channel_layout = av_get_default_channel_layout(OUTPUT_CHANNELS);
	this->audio_codec_context->sample_rate = metaDataStream->audio_codec_context->sample_rate;
	this->audio_codec_context->sample_fmt = metaDataStream->audio_codec->sample_fmts[0];
	this->audio_codec_context->bit_rate = OUTPUT_BIT_RATE1;
	this->audio_codec_context->time_base = av_rate;

	this->audio_codec_context->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

	this->audio_stream->time_base = this->audio_codec_context->time_base;

	if (avcodec_open2(this->audio_codec_context, this->audio_codec, NULL) < 0) { printf("could not open the codec"); return -1; }
	avcodec_parameters_from_context(this->audio_stream->codecpar, this->audio_codec_context);
	return 0;
}
static int init_output_frame(AVFrame** frame,
	AVCodecContext* output_codec_context,
	int frame_size)
{
	int error;

	if (!(*frame = av_frame_alloc())) {
		fprintf(stderr, "Could not allocate output frame\n");
		return AVERROR_EXIT;
	}

	(*frame)->nb_samples = frame_size;
	(*frame)->channel_layout = output_codec_context->channel_layout;
	(*frame)->format = output_codec_context->sample_fmt;
	(*frame)->sample_rate = output_codec_context->sample_rate;

	if ((error = av_frame_get_buffer(*frame, 0)) < 0) {
		fprintf(stderr, "Could not allocate output frame samples (error)\n");
		av_frame_free(frame);
		return error;
	}

	return 0;
}

static int64_t pts = 0;
int ffmpeg_stream::encode_video_frame(ffmpeg_packet packet) {
	if (packet.dec_frame) packet.dec_frame->pict_type = AV_PICTURE_TYPE_NONE;
	int ret;
	AVPacket* output_packet = av_packet_alloc();
	if (!output_packet) { printf("could not allocate memory for output packet"); return -1; }

	int response = avcodec_send_frame(this->video_codec_context, packet.dec_frame);
	
	while (response >= 0) {
		response = avcodec_receive_packet(this->video_codec_context, output_packet);
		av_frame_unref(packet.dec_frame);
		av_frame_free(&packet.dec_frame);
		if (response == AVERROR(EAGAIN)) {

			av_packet_unref(output_packet);
			av_packet_free(&output_packet);

			return response;
		}
		if (response == AVERROR_EOF) {
			return response;
		}
		else if (response < 0) {
			printf("Error while receiving packet from encoder: %s");
			return -1;
		}

		output_packet->stream_index = video_stream_idx;
		if (!packet.ptr_stream)
			return -1;
		output_packet->duration = this->video_stream->time_base.den / this->video_stream->time_base.num / packet.ptr_stream->video_stream->avg_frame_rate.num * packet.ptr_stream->video_stream->avg_frame_rate.den;

		av_packet_rescale_ts(output_packet, packet.ptr_stream->video_stream->time_base, this->video_stream->time_base);
		response = av_interleaved_write_frame(this->format_context, output_packet);

		if (response != 0) { printf("Error %d while receiving packet from decoder: %s", response); return -1; }
		
	}



	av_packet_unref(output_packet);
	av_packet_free(&output_packet);
	return 0;
}
ffmpeg_stream::~ffmpeg_stream()
{
	av_free(this->audio_codec);
	av_free(this->video_codec);


}
static int encode_audio_frame(AVFrame* frame,
	AVFormatContext* output_format_context,
	AVCodecContext* output_codec_context,
	int* data_present)
{
	AVPacket output_packet;
	int error;
	init_packet(&output_packet);


	if (frame) {
		frame->pts = pts;
		pts += frame->nb_samples;
	}

	error = avcodec_send_frame(output_codec_context, frame);
	if (error == AVERROR_EOF) {
		error = 0;
		goto cleanup;
	}
	else if (error < 0) {
		fprintf(stderr, "Could not send packet for encoding (error)\n");
		return error;
	}

	error = avcodec_receive_packet(output_codec_context, &output_packet);
	if (error == AVERROR(EAGAIN)) {
		error = 0;
		goto cleanup;
	}
	else if (error == AVERROR_EOF) {
		error = 0;
		goto cleanup;
	}
	else if (error < 0) {
		fprintf(stderr, "Could not encode frame (error)\n");
		goto cleanup;
	}
	else {
		*data_present = 1;
	}

	if (*data_present && (error = av_write_frame(output_format_context, &output_packet)) < 0) {
		fprintf(stderr, "Could not write frame (error)\n");
		goto cleanup;
	}

cleanup:
	av_packet_unref(&output_packet);
	return error;
}

static int load_encode_and_write(AVAudioFifo* fifo, AVFormatContext* output_format_context, AVCodecContext* output_codec_context)
{
	AVFrame* output_frame;
	const int frame_size = FFMIN(av_audio_fifo_size(fifo),
		output_codec_context->frame_size);
	int data_written;

	if (init_output_frame(&output_frame, output_codec_context, frame_size))
		return AVERROR_EXIT;

	if (av_audio_fifo_read(fifo, (void**)output_frame->data, frame_size) < frame_size) {
		fprintf(stderr, "Could not read data from FIFO\n");
		av_frame_free(&output_frame);
		return AVERROR_EXIT;
	}

	if (encode_audio_frame(output_frame, output_format_context, output_codec_context, &data_written)) {
		av_frame_free(&output_frame);
		return AVERROR_EXIT;
	}
	av_frame_free(&output_frame);
	return 0;
}

static int write_output_file_trailer(AVFormatContext* output_format_context)
{
	int error;
	if ((error = av_write_trailer(output_format_context)) < 0) {
		fprintf(stderr, "Could not write output file trailer (error)\n");
		return error;
	}
	return 0;
}

static int check_meta_data(SwrContext* resample_ctx, AVAudioFifo* fifo) {
	if (resample_ctx == NULL && fifo == NULL) {
		return 0;
	}
	return 1;
}

AVCodecContext* ffmpeg_stream::get_audio_codec_context()
{
	return this->audio_codec_context;
}
AVCodecContext* ffmpeg_stream::get_video_codec_context()
{
	return this->video_codec_context;
}
AVFormatContext* ffmpeg_stream::get_format_context()
{
	return this->format_context;
}
int ffmpeg_stream::get_frame_stream_id()
{

	return this->streaminParams.index_stream;
}
int ffmpeg_stream::open_stream(const char* path, open_mode mode, stream_mod format)
{
	if (mode == READ) {

		open_input_file(path, &this->format_context, &this->audio_codec_context,
			&this->video_codec_context, &this->video_codec, &this->audio_codec, &this->video_stream, &this->audio_stream, this->audio_stream_idx, this->video_stream_idx, format);
	}
	else {
		open_output_file(path, &this->format_context, &this->audio_codec_context, &this->output_codec, &this->streaminParams);
	}
	return 0;
}
int ffmpeg_stream::prepare_video_encoder(ffmpeg_stream* metaDataStream) {

	this->streaminParams.copy_audio = 0;
	this->streaminParams.copy_video = 0;
	this->streaminParams.video_codec = "libx264";
	this->streaminParams.audio_codec = "aac";
	this->streaminParams.codec_priv_key = "x264-params";
	this->streaminParams.codec_priv_value = "keyint=80:min-keyint=80:scenecut=0:force-cfr=1";
	this->video_stream = avformat_new_stream(this->format_context, NULL);

	this->video_codec = avcodec_find_encoder_by_name(this->streaminParams.video_codec);
	if (!this->video_codec) { printf("could not find the proper codec"); return -1; }

	this->video_codec_context = avcodec_alloc_context3(this->video_codec);
	if (!this->video_codec_context) { printf("could not allocated memory for codec context"); return -1; }

	av_opt_set(this->video_codec_context->priv_data, "preset", "ultrafast", 0);
	if (this->streaminParams.codec_priv_key && this->streaminParams.codec_priv_value)
		av_opt_set(this->video_codec_context->priv_data, this->streaminParams.codec_priv_key, this->streaminParams.codec_priv_value, 0);

	this->video_codec_context->height = metaDataStream->video_codec_context->height;
	this->video_codec_context->width = metaDataStream->video_codec_context->width;
	this->video_codec_context->sample_aspect_ratio = metaDataStream->video_codec_context->sample_aspect_ratio;
	if (this->video_codec->pix_fmts)
		this->video_codec_context->pix_fmt = this->video_codec->pix_fmts[0];
	else
		this->video_codec_context->pix_fmt = metaDataStream->video_codec_context->pix_fmt;

	this->video_codec_context->bit_rate = 1657927;//metaDataStream->video_codec_context->bit_rate;
	this->video_codec_context->rc_buffer_size = metaDataStream->video_codec_context->rc_buffer_size;
	this->video_codec_context->rc_max_rate = metaDataStream->video_codec_context->rc_max_rate;
	this->video_codec_context->rc_min_rate = metaDataStream->video_codec_context->rc_min_rate;
	AVRational input_framerate = av_guess_frame_rate(metaDataStream->format_context, metaDataStream->video_stream, NULL);

	this->video_codec_context->time_base = av_inv_q(input_framerate);

	this->video_stream->time_base = metaDataStream->video_stream->time_base;

	if (avcodec_open2(this->video_codec_context, this->video_codec, NULL) < 0) { printf("could not open the codec"); return -1; }
	avcodec_parameters_from_context(this->video_stream->codecpar, this->video_codec_context);
	if (this->streaminParams.copy_audio == 1) {
		if (prepare_copy(this->format_context, &this->audio_stream, metaDataStream->audio_stream->codecpar)) { return -1; }
	}
	else {
		prepare_audio_encoder(metaDataStream);
	}
	if (this->format_context->oformat->flags & AVFMT_GLOBALHEADER)
		this->format_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	if (!(this->format_context->oformat->flags & AVFMT_NOFILE)) {
		if (avio_open(&this->format_context->pb, this->format_context->url, AVIO_FLAG_WRITE) < 0) {
			printf("could not open the output file");
			return -1;
		}
	}

	AVDictionary* muxer_opts = NULL;

	if (this->streaminParams.muxer_opt_key && this->streaminParams.muxer_opt_value) {
		av_dict_set(&muxer_opts, this->streaminParams.muxer_opt_key, this->streaminParams.muxer_opt_value, 0);
	}
	int ret = 0;

	ret = avformat_write_header(this->format_context, &muxer_opts);
	if (ret < 0) {
		printf("an error occurred when opening output file");
		return -1;
	}



	return 0;
}

int ffmpeg_stream::prepare_metadata(ffmpeg_stream* metaDataStream)
{
	AVStream* stream = NULL;

	int error = 0;
	if (!(stream = avformat_new_stream(this->format_context, NULL))) {
		fprintf(stderr, "Could not create new stream\n");
		error = AVERROR(ENOMEM);
		return error;
	}

	this->audio_codec_context = avcodec_alloc_context3(this->output_codec);
	if (!this->audio_codec_context) {
		fprintf(stderr, "Could not allocate an encoding context\n");
		error = AVERROR(ENOMEM);
		return error;
	}

	this->audio_codec_context->channels = OUTPUT_CHANNELS;
	this->audio_codec_context->channel_layout = av_get_default_channel_layout(OUTPUT_CHANNELS);
	this->audio_codec_context->sample_rate = metaDataStream->audio_codec_context->sample_rate;
	this->audio_codec_context->sample_fmt = output_codec->sample_fmts[0];
	this->audio_codec_context->bit_rate = OUTPUT_BIT_RATE;
	check_sample_fmt(output_codec, output_codec->sample_fmts[0]);
	this->audio_codec_context->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

	stream->time_base.den = metaDataStream->audio_codec_context->sample_rate;
	stream->time_base.num = 1;

	if ((this->format_context)->oformat->flags & AVFMT_GLOBALHEADER)
		this->audio_codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	if ((error = avcodec_open2(this->audio_codec_context, output_codec, NULL)) < 0) {
		fprintf(stderr, "Could not open output codec (error)\n");
		return error;
	}

	error = avcodec_parameters_from_context(stream->codecpar, this->audio_codec_context);
	if (error < 0) {
		fprintf(stderr, "Could not initialize stream parameters\n");
		return error;
	}

	init_resampler(metaDataStream->audio_codec_context, this->audio_codec_context, &this->resample_context);
	init_fifo(&this->fifo, this->audio_codec_context);
	write_output_file_header(this->format_context);

	return error;
}

int ffmpeg_stream::p_read(read_frame_mode frame_mode, ffmpeg_packet* packet)
{
	AVPacket* audio_packet = av_packet_alloc();
	if (!audio_packet) { printf("could not allocate memory for audio packet"); return -1; }
	packet->packet = av_packet_alloc();

	AVFrame* frame = NULL;

	if (!packet->dec_frame) packet->dec_frame = av_frame_alloc();
	int data_present = 0;
	int finished = 0;
	int ret = 1;
	if (packet->ptr_stream)
		packet->dump_free();
	if (frame_mode == AUDIO_FRAME) {
		if (init_input_frame(&frame)) {
			return 0;
		}

		decode_audio_frame(frame, this->format_context, this->audio_codec_context, &data_present, &finished, &this->audio_stream_idx);
		if (finished) {
			ret = 1;
			packet->ptr_stream = this;
			packet->dec_frame = NULL;
			packet->finished = finished;
			packet->data_present = 0;
			return ret;
		}
		if (data_present) {
			packet->ptr_stream = this;
			packet->dec_frame = frame;
			packet->finished = finished;
			packet->data_present = data_present;


		}
	}
	if (frame_mode == AV_FRAME) {
		if (init_input_frame(&frame)) {
			return 0;
		}
		init_packet(audio_packet);
		decode_video_frame(frame, audio_packet, this->format_context, this->video_codec_context, this->audio_codec_context, &data_present, &finished, &this->video_stream_idx, &this->audio_stream_idx, &this->streaminParams);
		if (finished) {
			ret = 1;
			packet->ptr_stream = this;
			packet->dec_frame = frame;
			packet->packet = NULL;
			packet->finished = finished;
			packet->data_present = 0;
			return ret;
		}
		if (data_present) {
			packet->ptr_stream = this;
			packet->dec_frame = av_frame_alloc();
			packet->dec_frame = frame;
			packet->packet = NULL;
			packet->finished = finished;
			packet->data_present = data_present;
			packet->index = this->streaminParams.index_stream;
		}
		else {
			packet->ptr_stream = this;
			packet->dec_frame = NULL;
			packet->packet = audio_packet;
			packet->finished = finished;
			packet->data_present = data_present;


		}
	}
	if (frame_mode == VIDEO_FRAME) {

	}


	return ret;
}

int ffmpeg_stream::p_write_audio(ffmpeg_packet packet)
{
	if (!check_meta_data(this->resample_context, this->fifo)) {
		prepare_metadata(packet.ptr_stream);
	}

	if (packet.finished) {
		int data_written;
		do {
			data_written = 0;
			if (encode_audio_frame(NULL, this->format_context, this->audio_codec_context, &data_written))
				return 0;
		} while (data_written);
		return 0;
	}

	int output_frame_size = this->audio_codec_context->frame_size;

	if (packet.data_present) {
		uint8_t** converted_input_samples = NULL;
		if (init_converted_samples(&converted_input_samples, this->audio_codec_context, packet.dec_frame->nb_samples))
			return 0;

		if (convert_samples((const uint8_t**)packet.dec_frame->extended_data, converted_input_samples,
			packet.dec_frame->nb_samples, this->resample_context))
			return 0;

		if (add_samples_to_fifo(this->fifo, converted_input_samples, packet.dec_frame->nb_samples))
			return 0;

		if (converted_input_samples) {
			av_freep(&converted_input_samples[0]);
			free(converted_input_samples);
		}
	}
	int a = av_audio_fifo_size(this->fifo);
	if (av_audio_fifo_size(this->fifo) < output_frame_size) {
		return 1;
	}



	while (av_audio_fifo_size(this->fifo) >= output_frame_size || (packet.finished && av_audio_fifo_size(this->fifo) > 0))
	{
		if (load_encode_and_write(this->fifo, this->format_context, this->audio_codec_context)) {
			return 0;
		}

	}



	return 1;
}

int ffmpeg_stream::p_write_audio_1(ffmpeg_packet packet)
{

	if (!check_meta_data(this->resample_context, this->fifo)) {
		prepare_metadata(packet.ptr_stream);
	}
	int data_present;
	encode_audio_frame(packet.dec_frame, this->format_context, this->audio_codec_context, &data_present);
	if (data_present > 0) {
		printf("all ok");
	}
	else {
		printf("not encoding");
	}
	return 0;
}

int ffmpeg_stream::p_write_audio2(ffmpeg_packet packet)
{
	if (this->audio_codec_context == NULL) {
		prepare_audio_encoder(packet.ptr_stream);
	}
	AVPacket* output_packet = av_packet_alloc();
	if (!output_packet) { printf("could not allocate memory for output packet"); return -1; }

	int response = avcodec_send_frame(this->audio_codec_context, packet.dec_frame);

	while (response >= 0) {
		response = avcodec_receive_packet(this->audio_codec_context, output_packet);
		if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
			break;
		}
		else if (response < 0) {
			printf("Error while receiving packet from encoder: %s");
			return -1;
		}

		output_packet->stream_index = packet.ptr_stream->audio_stream_idx;

		av_packet_rescale_ts(output_packet, packet.ptr_stream->audio_stream->time_base, this->audio_stream->time_base);
		response = av_interleaved_write_frame(this->format_context, output_packet);
		if (response != 0) { printf("Error %d while receiving packet from decoder: %s", response); return -1; }
	}
	av_packet_unref(output_packet);
	av_packet_free(&output_packet);

	return 0;
}

int ffmpeg_stream::p_write_video(ffmpeg_packet packet)
{
	if (this->video_codec_context == NULL) {
		prepare_video_encoder(packet.ptr_stream);

	}
	if (packet.index == AVMEDIA_TYPE_AUDIO) {
		this->p_write_audio2(packet);
	}
	else if (packet.packet != NULL) {
		remux(&packet.packet, &this->format_context, packet.ptr_stream->audio_stream->time_base, this->audio_stream->time_base);
	}
	else {
		this->encode_video_frame(packet);
	}
	return 0;
}

int ffmpeg_stream::concate_video(ffmpeg_stream output, video_params* start, video_params* end)
{
	output.prepare_video_encoder(this);
	const char* path = this->format_context->url;
	ffmpeg_stream middle_end;
	if (start->type_template == 0) {
		start->end_concat_time = 2;
	}
	if (end->type_template == 0) {
		end->end_concat_time = end->time_size_second - 5;
	}
	int finished;
	int error;
	int count = 0;
	int sec = 0;
	int pts_v, dts_v;
	int pts_a, dts_a;
	int last_pts_v = 0;
	int last_dts_v = 0;
	int last_pts_a = 0;
	int last_dts_a = 0;
	avcodec_parameters_copy(output.video_stream->codecpar, this->video_stream->codecpar);

	while (true) {
		AVPacket input_packet;
		init_packet(&input_packet);
		if ((error = av_read_frame(this->format_context, &input_packet)) < 0) {

			if (error == AVERROR_EOF)
			{
				finished = 1;
				break;
			}
			else {
				fprintf(stderr, "Could not read frame (error)\n");
				return error;
			}
		}

		if (input_packet.stream_index == AVMEDIA_TYPE_VIDEO) {
			pts_v = input_packet.pts;
			dts_v = input_packet.dts;
		}
		if (input_packet.stream_index == AVMEDIA_TYPE_AUDIO) {
			pts_a = input_packet.pts;
			dts_a = input_packet.dts;

		}

		if (input_packet.stream_index == AVMEDIA_TYPE_VIDEO) {

			sec = input_packet.pts * av_q2d(output.video_stream->time_base);
			error = av_interleaved_write_frame(output.format_context, &input_packet);

		}


		if (sec >= start->end_concat_time) break;


	}

	avformat_close_input(&this->format_context);
	last_dts_v = dts_v + 500;
	last_pts_v = pts_v + 500;
	last_dts_a = dts_a + 500;
	last_pts_a = pts_a + 500;
	middle_end.open_stream(path, middle_end.READ, VIDEO);
	path = middle_end.format_context->url;
	count = 0;
	while (true) {
		AVPacket input_packet;
		init_packet(&input_packet);
		if ((error = av_read_frame(middle_end.format_context, &input_packet)) < 0) {

			if (error == AVERROR_EOF)
			{
				finished = 1;
				break;
			}
			else {
				fprintf(stderr, "Could not read frame (error)\n");
				return error;
			}
		}


		if (input_packet.stream_index == AVMEDIA_TYPE_VIDEO) {
			input_packet.pts += last_pts_v;
			input_packet.dts += last_dts_v;
			pts_v = input_packet.pts;
			dts_v = input_packet.dts;
		}
		if (input_packet.stream_index == AVMEDIA_TYPE_AUDIO) {
			input_packet.pts += last_pts_a;
			input_packet.dts += last_dts_a;
			pts_a = input_packet.pts;
			dts_a = input_packet.dts;
		}


		if (input_packet.stream_index == AVMEDIA_TYPE_VIDEO) {

			sec = input_packet.pts * av_q2d(output.video_stream->time_base);
		}
		error = av_interleaved_write_frame(output.format_context, &input_packet);

	}
	avformat_close_input(&middle_end.format_context);
	middle_end.open_stream(path, middle_end.READ, VIDEO);
	last_dts_v = dts_v + 500;
	last_pts_v = pts_v + 500;
	last_dts_a = dts_a + 500;
	last_pts_a = pts_a + 500;
	count = 0;
	while (true) {
		AVPacket input_packet;
		init_packet(&input_packet);
		if ((error = av_read_frame(middle_end.format_context, &input_packet)) < 0) {

			if (error == AVERROR_EOF)
			{
				finished = 1;
				return finished;
			}
			else {
				fprintf(stderr, "Could not read frame (error)\n");
				return error;
			}
		}


		if (input_packet.stream_index == AVMEDIA_TYPE_VIDEO) {
			input_packet.pts += last_pts_v;
			input_packet.dts += last_dts_v;
		}
		if (input_packet.stream_index == AVMEDIA_TYPE_AUDIO) {
			input_packet.pts += last_pts_a;
			input_packet.dts += last_dts_a;
		}

		if (input_packet.stream_index == AVMEDIA_TYPE_VIDEO) {

			sec = input_packet.pts * av_q2d(output.video_stream->time_base);
		}
		count = sec - this->size_per_second - start->time_size_second;
		if (input_packet.stream_index == AVMEDIA_TYPE_VIDEO)
			error = av_interleaved_write_frame(output.format_context, &input_packet);
		if (count >= end->end_concat_time)
			break;

	}
	return 0;
}

int ffmpeg_stream::get_time_size()
{
	if (this->format_context->duration != AV_NOPTS_VALUE) {
		int hours, mins, secs, us;
		int64_t duration = this->format_context->duration + 5000;
		secs = duration / AV_TIME_BASE;
		this->size_per_second = secs;

		us = duration % AV_TIME_BASE;
		mins = secs / 60;
		secs %= 60;
		hours = mins / 60;
		mins %= 60;
		av_log(NULL, AV_LOG_INFO, "%02d:%02d:%02d.%02d\n", hours, mins, secs, (100 * us) / AV_TIME_BASE);
	}
	return this->size_per_second;
}



double ffmpeg_stream::get_fps()
{
	return av_q2d(this->format_context->streams[video_stream_idx]->r_frame_rate);
}
int ffmpeg_stream::ffmpeg_stream_free()
{
	if (this->format_context != NULL) {
		avformat_free_context(this->format_context);
	}
	if (this->audio_codec_context != NULL) {
		avcodec_free_context(&this->audio_codec_context);
	}
	if (this->video_codec_context != NULL) {
		avcodec_free_context(&this->video_codec_context);
	}
	return 0;
}
int ffmpeg_stream::close_stream()
{
	if (write_output_file_trailer(this->format_context))
		return 0;
}



void ffmpeg_packet::dump()
{
	this->data_present = 0;
	this->finished = 0;
	this->index = 0;
	this->ptr_stream = NULL;
	this->packet = NULL;
	this->dec_frame = NULL;

}

void ffmpeg_packet::dump_free()
{
	this->data_present = 0;
	this->finished = 0;
	this->index = 0;
	if (this->ptr_stream)
		this->ptr_stream->~ffmpeg_stream();
	this->ptr_stream = NULL;
	this->packet = NULL;
	av_frame_unref(this->dec_frame);
	av_frame_free(&this->dec_frame);
}