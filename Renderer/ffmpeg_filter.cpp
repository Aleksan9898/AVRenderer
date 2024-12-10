#include "ffmpeg_filter.h"

static char* const get_error_text(const int error)
{
	static char error_buffer[255];
	av_strerror(error, error_buffer, sizeof(error_buffer));
	return error_buffer;
}

static int create_drawtext_desc(char* desc, ffmpeg_stream input_stream, int start, int end, int size, int type) {

	if (type == 0) {
		int count_char = 21;
		int name_x = input_stream.get_video_codec_context()->width / 1.922;
		int name_y = input_stream.get_video_codec_context()->height / 1.541;
		int name_h = input_stream.get_video_codec_context()->height / 8.918;
		int name_w = input_stream.get_video_codec_context()->width / 2.292;
		int name_font_size = name_w / count_char * 1.5;
		int info_x = name_x;
		int info_y = input_stream.get_video_codec_context()->height / 90 + name_y + name_h;
		int info_h = input_stream.get_video_codec_context()->height / 6.6;
		int info_w = name_w;
		int info_font_size = name_font_size;

		snprintf(desc, size, "drawbox=enable='between(t\,%d\,%d)':y=%d:x=%d:color=blue@0.1:width=%d:height=%d:t=fill,drawtext=enable='between(t\,%d\,%d)':fontfile=CENTURY.TTF:text='Aleqsanyan Aleqsanyan':fontcolor=white:fontsize=%d:x=%d+%d/2-text_w/2: y=%d+%d/2-text_h/2,\
drawbox=enable='between(t\,%d\,%d)':y=%d:x=%d:color=ORANGE:width=%d:height=%d:t=fill,drawtext=enable='between(t\,%d\,%d)':fontfile=CENTURY.TTF:text='Aleqsan Aleqsanyan':fontcolor=white:fontsize=%d:x=%d+%d/2-text_w/2:y=%d+text_h/2",
start, end, name_y, name_x, name_w, name_h, start, end, name_font_size, name_x, name_w, name_y, name_h, 
start, end, info_y, info_x, info_w, info_h, start, end, info_font_size + 2, info_x, info_w, info_y);
	}
	return 0;
}

ffmpeg_filter::~ffmpeg_filter()
{
	if (this->inp->inputs != 0)
		this->filter_free();
}

int ffmpeg_filter::init_filter(ffmpeg_stream* input_streams, ...)
{
	AVFilterContext* fil_ctx = NULL;
	const AVFilter* abuffer;
	AVCodecContext* input_codec_context = NULL;
	bool end = false;
	char args[512];
	int err;
	int input_number = 0;
	std::string src_name = "";
	this->filter_graph = avfilter_graph_alloc();
	if (!filter_graph) {
		av_log(NULL, AV_LOG_ERROR, "Unable to create filter graph.\n");
		return AVERROR(ENOMEM);
	}
	va_list arguments;
	va_start(arguments, input_streams);
	this->first_input_codec_context = input_streams;
	while (&input_streams != NULL)
	{
		input_codec_context = input_streams->get_audio_codec_context();
		abuffer = avfilter_get_by_name("abuffer");
		if (!abuffer) {
			av_log(NULL, AV_LOG_ERROR, "Could not find the abuffer filter.\n");
			return AVERROR_FILTER_NOT_FOUND;
		}

		if (!input_codec_context->channel_layout)
			input_codec_context->channel_layout = av_get_default_channel_layout(input_codec_context->channels);
		snprintf(args, sizeof(args), "sample_rate=%d:sample_fmt=%s:channel_layout=0x%llx", input_codec_context->sample_rate,
			av_get_sample_fmt_name(input_codec_context->sample_fmt), input_codec_context->channel_layout);

		input_number++;
		src_name = "src" + std::to_string(input_number);
		err = avfilter_graph_create_filter(&fil_ctx, abuffer, src_name.c_str(),
			args, NULL, filter_graph);
		if (err < 0) {
			av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer source\n");
			return err;
		}

		this->src.emplace_back(fil_ctx);
		memset(args, 0, sizeof(args));

		input_streams = va_arg(arguments, ffmpeg_stream*);
		if (end) {
			break;
		}
		if (*arguments == 0) {
			end = true;
		}
	}
	va_end(arguments);
	this->input_count = input_number;
	return 0;
}

int ffmpeg_filter::init_overlay_filter(ffmpeg_stream* input_streams, std::vector<video_params> overlay_duration, int duration_input)
{
	int repeat_count = 0;
	int frames_overlay = 0;
	int input_time_second = duration_input;

	char filter_desc[900];
	char repeat[50] = "";
	char draw_text[550];
	overlay_duration[1].start = overlay_duration[0].end;
	overlay_duration[1].end = overlay_duration[2].start;
	if (overlay_duration[0].type_template == 0) {
		if (overlay_duration[1].fps != 0 && overlay_duration[1].time_size_second != 0) {
			frames_overlay = overlay_duration[1].fps * overlay_duration[1].time_size_second-2;
			repeat_count = input_time_second / overlay_duration[1].time_size_second+1;
			snprintf(repeat, sizeof(repeat), "[wm1]loop=loop=%d:size=%d:start=0[wm1];", repeat_count, frames_overlay);
		}
		create_drawtext_desc(draw_text, *input_streams, overlay_duration[1].start, input_time_second, sizeof(draw_text), 0);
	}
	

	snprintf(filter_desc, sizeof(filter_desc), "movie=%s[wm];[wm]scale=%d:%d[wm];[in][wm]overlay=0:0:enable='between(t\,%d\,%d)'[out1];movie=%s[wm1];[wm1]scale=%d:%d[wm1];%s[wm1]setpts=0.9*PTS[wm1];[out1][wm1]overlay=0:0:enable='between(t\,%d\,%d)'[out2];movie=%s[wm2];[wm2]setpts=PTS-STARTPTS+%d/TB[wm2];[wm2]scale=%d:%d[wm2];[out2][wm2]overlay=0:0:enable='gte(t\,%d)'[out3];[out3]%s[out]",
		overlay_duration[0].path, input_streams->get_video_codec_context()->width,
		input_streams->get_video_codec_context()->height, overlay_duration[0].start, overlay_duration[0].end,
		overlay_duration[1].path, input_streams->get_video_codec_context()->width, input_streams->get_video_codec_context()->height, repeat,
		overlay_duration[1].start, overlay_duration[1].end,
		overlay_duration[2].path, overlay_duration[2].start - 6, input_streams->get_video_codec_context()->width,
		input_streams->get_video_codec_context()->height, overlay_duration[2].start - 6, draw_text);
	
	char args[128];
	int ret = 0;
	const AVFilter* buffersrc = avfilter_get_by_name("buffer");
	const AVFilter* buffersink = avfilter_get_by_name("buffersink");
	AVFilterInOut* outputs = avfilter_inout_alloc();
	AVFilterInOut* inputs = avfilter_inout_alloc();
	AVRational time_base = input_streams->get_format_context()->streams[AVMEDIA_TYPE_VIDEO]->time_base;
	enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE };
	char* dump;
	filter_graph = avfilter_graph_alloc();
	if (!outputs || !inputs || !filter_graph) {
		ret = AVERROR(ENOMEM);
		goto end;
	}

	snprintf(args, sizeof(args),
		"video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
		input_streams->get_video_codec_context()->width, input_streams->get_video_codec_context()->height,
		input_streams->get_video_codec_context()->pix_fmt, time_base.num, time_base.den,
		input_streams->get_video_codec_context()->sample_aspect_ratio.num, input_streams->get_video_codec_context()->sample_aspect_ratio.den);

	ret = avfilter_graph_create_filter(&this->inp, buffersrc, "in",
		args, NULL, filter_graph);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
		goto end;
	}

	ret = avfilter_graph_create_filter(&this->out, buffersink, "out",
		NULL, NULL, filter_graph);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
		goto end;
	}

	ret = av_opt_set_int_list(this->out, "pix_fmts", pix_fmts,
		AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
		goto end;
	}

	outputs->name = av_strdup("in");
	outputs->filter_ctx = this->inp;
	outputs->pad_idx = 0;
	outputs->next = NULL;


	inputs->name = av_strdup("out");
	inputs->filter_ctx = this->out;
	inputs->pad_idx = 0;
	inputs->next = NULL;

	if ((ret = avfilter_graph_parse_ptr(this->filter_graph, filter_desc,
		&inputs, &outputs, NULL)) < 0)
		goto end;

	if ((ret = avfilter_graph_config(this->filter_graph, NULL)) < 0)
		goto end;
	dump = avfilter_graph_dump(this->filter_graph, NULL);
	av_log(NULL, AV_LOG_ERROR, "Graph :\n%s\n", dump);
end:
	avfilter_inout_free(&inputs);
	avfilter_inout_free(&outputs);

	return ret;

	
}


int ffmpeg_filter::init_overlay_filter1(ffmpeg_stream* input_streams, video_params overlay_duration, int duration_input, overlay_mod ov_mode)
{
	int repeat_count = 0;
	int frames_overlay = 0;
	int input_time_second = duration_input;

	char filter_desc[900];
	char repeat[50] = "";
	char draw_text[550];
	switch (ov_mode)
	{
	case 0:
		snprintf(filter_desc, sizeof(filter_desc), "movie=%s[wm];[wm]scale=%d:%d[wm];[in][wm]overlay=0:0[out]",
			overlay_duration.path, input_streams->get_video_codec_context()->width,
			input_streams->get_video_codec_context()->height);
		break;
	case 1:
		snprintf(filter_desc, sizeof(filter_desc), "movie=%s[wm];[wm]scale=%d:%d[wm];[in][wm]overlay=0:0:enable='between(t\,%d\,%d)'[out]",
			overlay_duration.path, input_streams->get_video_codec_context()->width,
			input_streams->get_video_codec_context()->height, overlay_duration.start, overlay_duration.end);
		break;
	case 2:
		
		if (overlay_duration.type_template == 0) {
			if (overlay_duration.fps != 0 && overlay_duration.time_size_second != 0) {
				frames_overlay = overlay_duration.fps * overlay_duration.time_size_second;
				repeat_count = input_time_second / overlay_duration.time_size_second;
				snprintf(repeat, sizeof(repeat), "[wm]loop=loop=%d:size=%d:start=0[wm];", repeat_count, frames_overlay);
			}
			create_drawtext_desc(draw_text, *input_streams, overlay_duration.start, overlay_duration.end, sizeof(draw_text), 0);
			snprintf(filter_desc, sizeof(filter_desc), "movie=%s[wm];[wm]scale=%d:%d[wm];%s[wm]setpts=0.9*PTS[wm];[in][wm]overlay=0:0:enable='between(t\,%d\,%d)'[out1];[out1]%s[out]",
				overlay_duration.path, input_streams->get_video_codec_context()->width,
				input_streams->get_video_codec_context()->height, repeat, overlay_duration.start, overlay_duration.end-6,draw_text);
		}
		
		break;
	case 3:
		snprintf(filter_desc, sizeof(filter_desc), "movie=%s[wm];[wm]setpts=PTS-STARTPTS+%d/TB[wm];[wm]scale=%d:%d[wm];[in][wm]overlay=0:0:enable='gte(t\,%d)'[out]",
			overlay_duration.path, overlay_duration.start - 6, input_streams->get_video_codec_context()->width,
			input_streams->get_video_codec_context()->height, overlay_duration.start - 6);
		break;
	}
	
	char args[128];
	int ret = 0;
	const AVFilter* buffersrc = avfilter_get_by_name("buffer");
	const AVFilter* buffersink = avfilter_get_by_name("buffersink");
	AVFilterInOut* outputs = avfilter_inout_alloc();
	AVFilterInOut* inputs = avfilter_inout_alloc();
	AVRational time_base = input_streams->get_format_context()->streams[AVMEDIA_TYPE_VIDEO]->time_base;
	enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE };
	char* dump;
	filter_graph = avfilter_graph_alloc();
	if (!outputs || !inputs || !filter_graph) {
		ret = AVERROR(ENOMEM);
		goto end;
	}

	snprintf(args, sizeof(args),
		"video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
		input_streams->get_video_codec_context()->width, input_streams->get_video_codec_context()->height,
		input_streams->get_video_codec_context()->pix_fmt, time_base.num, time_base.den,
		input_streams->get_video_codec_context()->sample_aspect_ratio.num, input_streams->get_video_codec_context()->sample_aspect_ratio.den);

	ret = avfilter_graph_create_filter(&this->inp, buffersrc, "in",
		args, NULL, filter_graph);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
		goto end;
	}

	ret = avfilter_graph_create_filter(&this->out, buffersink, "out",
		NULL, NULL, filter_graph);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
		goto end;
	}

	ret = av_opt_set_int_list(this->out, "pix_fmts", pix_fmts,
		AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
		goto end;
	}

	outputs->name = av_strdup("in");
	outputs->filter_ctx = this->inp;
	outputs->pad_idx = 0;
	outputs->next = NULL;


	inputs->name = av_strdup("out");
	inputs->filter_ctx = this->out;
	inputs->pad_idx = 0;
	inputs->next = NULL;

	if ((ret = avfilter_graph_parse_ptr(this->filter_graph, filter_desc,
		&inputs, &outputs, NULL)) < 0)
		goto end;

	if ((ret = avfilter_graph_config(this->filter_graph, NULL)) < 0)
		goto end;
	dump = avfilter_graph_dump(this->filter_graph, NULL);
	av_log(NULL, AV_LOG_ERROR, "Graph :\n%s\n", dump);
end:
	avfilter_inout_free(&inputs);
	avfilter_inout_free(&outputs);

	return ret;

	return 0;
}

int ffmpeg_filter::prepare_output()
{
	int err;
	AVFilterContext* mix_ctx;
	const AVFilter* mix_filter;
	const AVFilter* abuffers_out;
	char args[512];
	mix_filter = avfilter_get_by_name("amix");
	if (!mix_filter) {
		av_log(NULL, AV_LOG_ERROR, "Could not find the mix filter.\n");
		return AVERROR_FILTER_NOT_FOUND;
	}
	std::string input_cnt = "inputs=" + std::to_string(this->input_count);
	snprintf(args, sizeof(args), input_cnt.c_str());

	err = avfilter_graph_create_filter(&mix_ctx, mix_filter, "amix",
		args, NULL, this->filter_graph);

	if (err < 0) {
		av_log(NULL, AV_LOG_ERROR, "Cannot create audio amix filter\n");
		return err;
	}


	abuffers_out = avfilter_get_by_name("abuffersink");
	if (!abuffers_out) {
		av_log(NULL, AV_LOG_ERROR, "Could not find the abuffersink filter.\n");
		return AVERROR_FILTER_NOT_FOUND;
	}

	this->out = avfilter_graph_alloc_filter(this->filter_graph, abuffers_out, "out");
	if (!this->out) {
		av_log(NULL, AV_LOG_ERROR, "Could not allocate the abuffersink instance.\n");
		return AVERROR(ENOMEM);
	}


	int sample_fmt[2] = { AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_NONE };
	err = av_opt_set_int_list(this->out, "sample_fmts", (sample_fmt), AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN);

	uint8_t ch_layout[64];
	av_get_channel_layout_string((char*)ch_layout, sizeof(ch_layout), 0, OUTPUT_CHANNELS);
	av_opt_set(this->out, "channel_layout", (char*)ch_layout, AV_OPT_SEARCH_CHILDREN);

	if (err < 0) {
		av_log(NULL, AV_LOG_ERROR, "Could set options to the abuffersink instance.\n");
		return err;
	}

	err = avfilter_init_str(this->out, NULL);
	if (err < 0) {
		av_log(NULL, AV_LOG_ERROR, "Could not initialize the abuffersink instance.\n");
		return err;
	}


	int i = 0;
	for (AVFilterContext* src_ctx : this->src) {
		err = avfilter_link(src_ctx, 0, mix_ctx, i);
		i++;
		if (err < 0) {
			av_log(NULL, AV_LOG_ERROR, "Error connecting filters\n");
			return err;
		}
	}

	if (err >= 0)
		err = avfilter_link(mix_ctx, 0, this->out, 0);
	if (err < 0) {
		av_log(NULL, AV_LOG_ERROR, "Error connecting filters\n");
		return err;
	}


	err = avfilter_graph_config(this->filter_graph, NULL);
	if (err < 0) {
		av_log(NULL, AV_LOG_ERROR, "Error while configuring graph : %s\n", get_error_text(err));
		return err;
	}

	char* dump = avfilter_graph_dump(this->filter_graph, NULL);
	av_log(NULL, AV_LOG_ERROR, "Graph :\n%s\n", dump);

	return 0;
}

int ffmpeg_filter::merge_audio(ffmpeg_packet* out_packet, ffmpeg_packet input_packet, ...)
{
	if (this->out == NULL) {
		prepare_output();
	}
	int ret = 0;
	int data_present_in_graph = 0;

	out_packet->ptr_stream = this->first_input_codec_context;

	AVFilterContext** buffer_contexts = new AVFilterContext * [this->input_count];
	int* input_finished = new int[this->input_count];
	memset(input_finished, 0, sizeof(input_finished));
	int* input_to_read = new int[this->input_count];
	memset(input_to_read, 1, sizeof(input_to_read));

	va_list input_packets;
	va_start(input_packets, input_packet);

	for (int i = 0; i < this->input_count; i++) {
		buffer_contexts[i] = this->src[i];
		if (input_finished[i] || input_to_read[i] == 0) {
			continue;
		}

		input_to_read[i] = 0;

		if (input_packet.finished && !input_packet.data_present) {
			input_finished[i] = 1;

			ret = 0;
			av_log(NULL, AV_LOG_INFO, "Input n°%d finished. Write NULL frame \n", i);

			ret = av_buffersrc_write_frame(buffer_contexts[i], NULL);
			if (ret < 0) {
				av_log(NULL, AV_LOG_ERROR, "Error writing EOF null frame for input %d\n", i);
				return ret;
			}
		}
		else if (input_packet.data_present) {
			ret = av_buffersrc_write_frame(buffer_contexts[i], input_packet.dec_frame);
			if (ret < 0) {
				av_log(NULL, AV_LOG_ERROR, "Error while feeding the audio filtergraph\n");
				return ret;
			}

		}

		data_present_in_graph = input_packet.data_present | data_present_in_graph;
		if (i != this->input_count - 1)
			input_packet = va_arg(input_packets, ffmpeg_packet);
	}

	if (data_present_in_graph) {
		out_packet->dec_frame = av_frame_alloc();
		while (1) {
			ret = av_buffersink_get_frame(this->out, out_packet->dec_frame);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				for (int i = 0; i < this->input_count; i++) {
					if (av_buffersrc_get_nb_failed_requests(buffer_contexts[i]) > 0) {
						input_to_read[i] = 1;
					}
				}
				break;
			}
			if (ret < 0)
				return ret;
		}
	}
	else {
		av_log(NULL, AV_LOG_INFO, "No data in graph\n");
		for (int i = 0; i < this->input_count; i++) {
			input_to_read[i] = 1;
		}
	}
	va_end(input_packets);



	return ret;
}

int ffmpeg_filter::overlay_video(ffmpeg_packet* out_packet, ffmpeg_packet input_packet)
{
	if (av_buffersrc_add_frame_flags(this->inp, input_packet.dec_frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
		av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
		return -1;
	}

	out_packet->dec_frame = av_frame_alloc();

	int ret = av_buffersink_get_frame(this->out, out_packet->dec_frame);
	
	
	if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
		return ret;
	if (ret < 0)
		return ret;
	out_packet->ptr_stream = input_packet.ptr_stream;
	out_packet->index = AVMEDIA_TYPE_VIDEO;
	return 0;
}

int ffmpeg_filter::get_frame_time_pos(ffmpeg_packet input_packet)
{
	int64_t duration = input_packet.dec_frame->pkt_pos + 5000;
	int secs = duration / AV_TIME_BASE;
	return secs;
}

int ffmpeg_filter::filter_free()
{
		avfilter_free(this->out);
		avfilter_free(this->inp);
		avfilter_graph_free(&this->filter_graph);

	return 0;
}
