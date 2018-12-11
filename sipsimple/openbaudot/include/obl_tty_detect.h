#ifndef __OBL__
#define __OBL__

#ifdef __cplusplus
extern "C" {
#endif

#define TTY_AUDIO_SAMPLE_SIZE 22

typedef struct {
	short audio_sample[TTY_AUDIO_SAMPLE_SIZE];
	int count_audio_sample;
	int max_tty;
	int count_tty;
	int last_tty;
	int total_audio_samples;
} OBL_TTY_DETECT;

void init_check_for_tty(OBL_TTY_DETECT * obl_tty_detect);
int check_for_tty(OBL_TTY_DETECT * obl_tty_detect, char byte1, char byte2);

#ifdef __cplusplus
}
#endif


#endif
