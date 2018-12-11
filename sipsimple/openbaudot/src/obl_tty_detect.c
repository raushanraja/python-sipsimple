/*-----------------------------------------------------------------------*\

  FILE...: func_test.c
  AUTHOR.: David Rowe
  Created: 27 August 2007

  Simple functional test for Open Baudot Library.

\*-----------------------------------------------------------------------*/

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "../include/obl_tty_detect.h"

#define N 160
#define SAMPLEFREQUENCY 8000

#ifdef __cplusplus
extern "C" {
#endif

double goertzelFilter(short * samples, double freq, int noOfSamples) {
    double s_prev = 0.0;
    double s_prev2 = 0.0;
    double coeff,normalizedfreq,power,s;
    int i;
    //printf("noOfSamples is %d\n", noOfSamples);
    normalizedfreq = freq / SAMPLEFREQUENCY;
    coeff = 2*cos(2*M_PI*normalizedfreq);
    for (i=0; i<noOfSamples; i++) {
	//printf("%d ", samples[i]);
        s = samples[i] + coeff * s_prev - s_prev2;
        s_prev2 = s_prev;
        s_prev = s;
    }
    //printf("\n");
    power = s_prev2*s_prev2+s_prev*s_prev-coeff*s_prev*s_prev2;
    return power;
}

void init_check_for_tty(OBL_TTY_DETECT * obl_tty_detect)
{
	assert(obl_tty_detect != NULL);

	memset(obl_tty_detect,0,sizeof(OBL_TTY_DETECT));
}

int check_for_tty(OBL_TTY_DETECT * obl_tty_detect, char byte1, char byte2)
{
	short sample;
	char data[2];
	double power1400;
	double power1800;
	int ret = 0;

	data[0] = byte1;
	data[1] = byte2;

	sample = (*(short *)data);
	//printf("check sample %d\n", sample);
	obl_tty_detect->audio_sample[obl_tty_detect->count_audio_sample] = sample;
	obl_tty_detect->count_audio_sample++;
	if (obl_tty_detect->count_audio_sample == TTY_AUDIO_SAMPLE_SIZE) {
		obl_tty_detect->total_audio_samples++;
		obl_tty_detect->count_audio_sample = 0;
		power1800 = goertzelFilter(obl_tty_detect->audio_sample, 1800, TTY_AUDIO_SAMPLE_SIZE);
    		power1400 = goertzelFilter(obl_tty_detect->audio_sample, 1400, TTY_AUDIO_SAMPLE_SIZE);
		//printf("power1800 %f, power1400 %f\n", power1800, power1400);
		if ((power1800 > 10163784840) || (power1400 > 9062720509)) {
    			//printf("sample ending at %d is 1800\n", count);
			if (obl_tty_detect->last_tty == (obl_tty_detect->total_audio_samples - 1)) {
				obl_tty_detect->count_tty++;
			} else {
				obl_tty_detect->count_tty = 0;
			}
			if (obl_tty_detect->max_tty < obl_tty_detect->count_tty) {
				obl_tty_detect->max_tty = obl_tty_detect->count_tty;
			}
			obl_tty_detect->last_tty = obl_tty_detect->total_audio_samples;
		}
	}
	if (obl_tty_detect->max_tty >= 64) {
		ret = 1;
	}
	return ret;
}

#ifdef __cplusplus
}
#endif
