#include "renderer.h"

int main(int argc, char** argv)
{
	if (argc != 5) {
		fprintf(stderr, "no matching arguments");
		return 0;
	}

	int template_type = 0;
	const char* filename =  argv[1];
	const char* filename1 = argv[2];
	const char* out_filename = argv[4];
	const char* start_template_name = "Start.avi";
	const char* middle_template_name = "Middle.avi";
	const char* end_template_name = "End.avi";
	ffmpeg_filter f;
	ffmpeg_stream start;
	ffmpeg_stream middle;
	ffmpeg_stream end;
	video_params start_params;
	video_params middle_params;
	video_params end_params;
	ffmpeg_stream input_exmpl;
	ffmpeg_packet input_exmpl_packet;
	ffmpeg_stream input_exmpl2;
	ffmpeg_packet input_exmpl_packet2;
	ffmpeg_stream output_exmpl;
	ffmpeg_packet output_exmpl_packet;
	ffmpeg_filter overlay_video;
	std::vector<video_params> params;
	start_params.type_template = template_type;
	middle_params.type_template = template_type;
	end_params.type_template = template_type;

	input_exmpl.open_stream(filename, input_exmpl.READ, VIDEO);
	input_exmpl2.open_stream(filename1, input_exmpl2.READ, AUDIO);
	output_exmpl.open_stream(out_filename, output_exmpl.WRITE, VIDEO);

	f.init_filter(&input_exmpl, &input_exmpl2, NULL);
	f.prepare_output();

	start.open_stream(start_template_name, start.READ, VIDEO);
	calc_duretion(start, &start_params, start_template_name);
	start_params.start = 0;
	start_params.end = start_params.time_size_second;
	
	middle.open_stream(middle_template_name, middle.READ, VIDEO);
	calc_duretion(middle, &middle_params, middle_template_name);

	end.open_stream(end_template_name,end.READ,VIDEO);
	calc_duretion(end, &end_params, end_template_name);
	end_params.start = start_params.time_size_second + input_exmpl.get_time_size();
	end_params.end = start_params.time_size_second + input_exmpl.get_time_size() + end_params.time_size_second;
	int input_time = input_exmpl.get_time_size();
	input_exmpl.concate_video(output_exmpl,&start_params,&end_params);
	output_exmpl.close_stream();
	input_exmpl.ffmpeg_stream_free();
	output_exmpl.ffmpeg_stream_free();
	start.ffmpeg_stream_free();
	middle.ffmpeg_stream_free();
	end.ffmpeg_stream_free();
	input_exmpl.open_stream("renderer.mp4", input_exmpl.READ, VIDEO);
	output_exmpl.open_stream("final.mp4", output_exmpl.WRITE, VIDEO);
	AVFrame* frame = av_frame_alloc();

	bool t = true;
	params.emplace_back(start_params);
	params.emplace_back(middle_params);
	params.emplace_back(end_params);

	overlay_video.init_overlay_filter(&input_exmpl, params,input_time);
	
	while (input_exmpl.p_read(input_exmpl.AV_FRAME, &input_exmpl_packet)) {
	
		if ((input_exmpl_packet.data_present == 1 || input_exmpl_packet.finished == 1)) {

			if (input_exmpl_packet.index == AVMEDIA_TYPE_AUDIO) {
				if (t == false) {
					f.merge_audio(&output_exmpl_packet, input_exmpl_packet, frame);

				}
				output_exmpl_packet.index = input_exmpl_packet.index;

				if (t)
				{
					input_exmpl2.p_read(input_exmpl2.AUDIO_FRAME, &input_exmpl_packet2);
				}
				if (input_exmpl_packet2.finished == 1) {
					f.merge_audio(&output_exmpl_packet, input_exmpl_packet, input_exmpl_packet2);
					input_exmpl_packet2.finished = 2;
					t = false;
				}


				if (input_exmpl_packet2.data_present == 1 && input_exmpl_packet2.finished == 0) {
					f.merge_audio(&output_exmpl_packet, input_exmpl_packet, input_exmpl_packet2);
					output_exmpl_packet.index = input_exmpl_packet.index;

				}
			}
			else {

				if (input_exmpl_packet.data_present == 1) {
					overlay_video.overlay_video(&output_exmpl_packet, input_exmpl_packet);
					input_exmpl_packet.dump_free();
			

				}
			}
			if (input_exmpl_packet.finished != 1)
					output_exmpl.p_write_video(output_exmpl_packet);
				
			if (input_exmpl_packet.finished == 1 && input_exmpl_packet.data_present == 0) {
				
				break;
			}
			
		}

	}

	output_exmpl.close_stream();


}




