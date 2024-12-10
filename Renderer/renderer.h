#pragma once
#include "ffmpeg_filter.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <ctime>

static int calc_duretion(ffmpeg_stream str, video_params* params, const char* path = NULL) {
	params->time_size_second = str.get_time_size();
	params->fps = str.get_fps();
	params->path = path;
	return 0;
}