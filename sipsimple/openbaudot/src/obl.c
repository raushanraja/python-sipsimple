/*
 * OBL openbaudot Library
 * 
 * Copyright (C) 2007-2008 Board of Regents of the University of Wisconsin 
 *                   System (Univ. of Wisconsin-Madison, Trace R&D Center)
 * Copyright (C) 2007-2008 Omnitor AB
 * Copyright (C) 2007-2008 Voiceriver Inc
 *
 * This software was developed with support from the National Institute on
 * Disability and Rehabilitation Research, US Dept of Education under Grant
 * # H133E990006 and H133E040014  
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Please send a copy of any improved versions of the library to: 
 * Jeff Knighton, Voiceriver Inc, jeff.knighton@voiceriver.com
 * Gunnar Hellstrom, Omnitor AB, Box 92054, 12006 Stockholm, SWEDEN
 * Gregg Vanderheiden, Trace Center, U of Wisconsin, Madison, Wi 53706
 *
 * file...: obl.c
 * original author.: David Rowe
 * Created: 6 September 2007
 * 
 */

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../include/obl.h"

#define FS      48000      /* sampling rate                 */
#define NB      32        /* number of Baudot codes        */
#define NBITS   5         /* num of bits in Baudot codes   */
#define LETFLAG (1<<6)    /* flag in ascii_to_baudot[]     */
#define FIGFLAG (1<<5)    /* flag in ascii_to_baudot[]     */

#define LETR    0x1f      /* letters Baudot code           */
#define FIGR    0x1b      /* figures Baudot code           */

#define CRLF_THRESH 60    /* start looking for space       */
#define CRLF_FORCE  72    /* force CR-LF                   */

#define DEM_BAUD    47    /* works for both 50 and 45 baud */

/* obl_tx_queue state machine states */

#define LETTERS   0       /* letters mode/state            */
#define FIGURES   1       /* figures mode/state            */
#define SEND_LF   2       /* send line feed state          */
#define SEND_LETR 3
#define SEND_FIGR 4

/* obl_modulate() state machine states */

#define MOD_ST_IDLE  0    /* no data in mod buffer, idling */  
#define MOD_ST_START 1    /* modulating start bit          */
#define MOD_ST_BIT   2    /* modulating a baudot bit       */
#define MOD_ST_STOP  3    /* modulating stop bit           */
#define MOD_ST_HOLD  4    /* modulating hold tone          */

/* 200ms hold time in samples */
#define MOD_HOLD_SAM (200*FS/1000)

#define PI      3.141593
#define W_ONE  (2*PI*1400/FS)  /* 1 freq in normalised rads   */
#define W_ZERO (2*PI*1800/FS)  /* 0 freq in normalised rads   */
#define AMP     16384          /* default peak mod amplitude  */
#define BETA    0.95           /* filter bandwidth factor     */
#define SINE_LUT_SZ 16384

/* obl_demodulate() state machine states */

#define DEMOD_ST_WAIT_START 0    /* wait for start bit     */  
#define DEMOD_ST_SAMPLE     1    /* sampling demod bits    */
#define DEMOD_ST_WAIT_STOP  2    /* wait for stop bit      */

#define MIN_THRESH          3    /* ratio for vaild signal */

/* auto baud detection state machine states */

#define AUTOBD_ST_WAIT_START 0
#define AUTOBD_ST_WAIT_ZEROX 1
#define AUTOBD_ST_WAIT_STOP  2
#define AUTOBD_ST_DISABLED   3


#define	REPLACEMENT_CHAR    '\'' /* an apostaphe is the replacement for 
					characters not represented in baudot.*/

#define OBL_TRUE    1
#define OBL_FALSE   0


// #ifdef _WIN32
	#define INLINE
/*
#else
	#define INLINE inline
#endif
*/
/* precomputed constants for demod_dsp */

#define FIXED_PT
#ifdef FIXED_PT
#define C1 ((2.0*cos(W_ONE)*BETA) * 32768)
#define C2 ((BETA*BETA) * 32768)
#define C3 ((2.0*cos(W_ZERO)*BETA) * 32768)
static short c1;
static short c2;
static short c3;
#endif

/* top level state machines states */

#define TOP_STATE_RESET 0   /* we have just been reset    */
#define TOP_STATE_DEMOD 1   /* we are in modulate state   */
#define TOP_STATE_MOD   2   /* we are in demodulate state */

#define TOP_TIMEOUT     (200*FS/1000) /* 200 ms in samples */

/* given the mode and Baudot code, return ASCII char */

static const char baudot_to_ascii[2][32]={
	{
		0x08,'E','\n','A',' ','S','I','U',
		'\r','D','R' ,'J','N','F','C','K',
		'T','Z','L' ,'W','H','Y','P','Q',
		'O','B','G' ,'^','M','X','V','^',
	},
	{
		0x08,'3','\n','-',' ',',','8','7',
		'\r','$','4' , 0x27 ,',','!',':','(',
		'5','"',')' ,'2','=','6','0','1',
		'9','?','+' ,'^','.','/',';','^'
	}
};

/*
  Sine look up table (LUT) for efficient modulator implementation.
  This table is generated at init time.  The index to the table is an
  angle in Q14 format, the range of 0..2pi is represented in 16384
  steps.  The output of the table is the sine of the nagle represented
  in Q15 format, ie the -1.0..1.0 range scaled up to -32767..32767.
*/

static short sin_lut_q15[SINE_LUT_SZ];

/* The UTF-8 character set is a superset of ASCII.  ASCII defines the 
 * characters with the single byte value of 0-127.  UTF-8 defines a much 
 * larger character set by allowing a single character to be represented by 
 * upwards of 4 bytes.  The rules for determing multi-byte UTF-8 characters
 * are shown as follows: 
 *
 *    byte0     byte1     byte2    byte3
 *  ----------------------------------------
 *   0XXXXXXX
 *   110XXXXX  10XXXXXX
 *   1110XXXX  10XXXXXX  10XXXXXX
 *   11110XXX  10XXXXXX  10XXXXXX  10XXXXXX
 *
 *  For the purposes of transcoding UTF-8 to baudot, we have adopted the policy
 *  that characters which don't have an intuitive mapping to a baudot character
 *  will be represented as a single apostrophe.
 */

 /** 
 * This method will return a valid ascii character or NULL if we're at the 
 * end of the string.
 * @param utf8data the null-terminated utf8-encoded data
 * @param pincrement a pointer to an increment value.  The value of the integer this
 *        points to will contain the number of bytes to that need to be incremented
 *		  to point to the next valid utf8 character.  This will typically return a 
 *		  1 for ascii characters, and may return up to 4 for an uncommon utf8 char.
 */
char utf8_to_ascii(const char* data,int *pincrement)
{
	/*assume that we will NOT have a valid baudot character*/
	char ascii = REPLACEMENT_CHAR;
	char current = *data;
	int increment = 0;
	increment = 1;
	
	/* see if it's a 4,3 or 2  byte representation */
        if ((current & 0xF0) == 0xF0)
		increment = 4; /*skip past the 4 byte character*/  
	else if ((current & 0xE0) == 0xE0) 
		increment = 3; /*skip past the 3 byte character*/ 
	else if ((current & 0xC0) == 0xC0)
		increment = 2; /*skip past the 2 byte character*/
	else
	{
		increment = 1;/*skip past the 1 byte character*/
		ascii = current;
	}
	if (pincrement)
		*pincrement = increment;
	/*printf("utf8 char=%c\n",ascii);*/
	return ascii;		 
}

/*
  Given the ASCII char, return:

     Baudot code..........: bit 4..0
     Letters flag.........: bit 5
     Figures flag.........: bit 6

   If there is no Baudot code for the input ASCII, then the Baudot
   code for a space is returned.  The letters flag is set if the ASCII
   char maps to a Baudot code in the letters table.  The figures flag
   is set if the ASCII char maps to a Baudot code in the figures
   table.  Note that some ASCII chars map to codes in both tables
   (e.g. space, \r, \n, LETR, FIGR) so both flags will be set.
*/

static char ascii_to_baudot[256];

void init_ascii_to_baudot() 
{
	int i,j;
	int found_letters;
	int found_numbers;

	for(j=0; j<256; j++) 
	{
		/* default to apostrophe */

		ascii_to_baudot[j] = FIGFLAG | 0x0b;

		/* search for matching LETTERS Baudot code */

		found_letters = 0;
		for(i=0; i<NB; i++) 
			if ((j == baudot_to_ascii[LETTERS][i]) && !found_letters)
			{
				ascii_to_baudot[j] = LETFLAG | i;
				found_letters = 1;
			}
		
		/* search for matching FIGURES Baudot code */

		found_numbers = 0;
		for(i=0; i<NB; i++) 
			if ((j == baudot_to_ascii[FIGURES][i]) && !found_numbers)
			{
				if (!found_letters) 
					ascii_to_baudot[j] = FIGFLAG | i;
				else
					ascii_to_baudot[j] |= FIGFLAG;
				found_numbers = 1;
			}
	}

}

/*
  TODO: Do we need a more accruate specification of baud rate, e.g. 45.5 rather than 45,
  and 47.7 rather than 47?
*/

void obl_init(OBL *obl, int baud, int (*my_callback)(void* obl, int event, int data))
{
	int i;

	assert(obl != NULL);
	assert(my_callback != NULL);

	memset(obl,0,sizeof(OBL));
	obl->callback = my_callback; 

	for(i=0; i<SINE_LUT_SZ; i++)
		sin_lut_q15[i] = 32767.0*sin(2.0*PI*i/SINE_LUT_SZ);

	init_ascii_to_baudot();

	/* Pre-compute some constants */

#ifdef FIXED_PT
	c1 = C1;
	c2 = C2;
	c3 = C3;
#endif

	/* regular reset */
	obl->autobaud_enabled = 1;
	obl_reset(obl, baud);
}

void obl_reset(OBL *obl, int baud)
{
	int i;

	assert(obl != NULL);

	memset(obl->mod_buffer, OBL_TEXT_BUF, 0);
	obl->pmod_buffer_in  = obl->mod_buffer;
	obl->pmod_buffer_out = obl->mod_buffer;
	obl->mod_buffer_count = 0;
	obl->mod_chars_without_crlf = 0;
	obl->mod_chars_without_case = 0;
	obl->mod_state = MOD_ST_IDLE;
	obl->mod_baud = baud;
	obl->mod_samples_per_bit = FS/obl->mod_baud;		
	obl->mod_num_stop_bits = OBL_STOP_1_5;
	obl->mod_num_stop_samples = 
		obl->mod_num_stop_bits*obl->mod_samples_per_bit/2;		
	obl->mod_crlf = OBL_CRLF_ON;
	obl->mod_baud = baud;
	obl->mod_case = OBL_NO_CASE;
	obl->mod_qstate = 0;
	obl->mod_phase = 0;
	obl->mod_one_amp = AMP;
	obl->mod_zero_amp = AMP;
	obl->w_one = W_ONE;
	obl->w_one_q16 = (65536/(2.0*PI))*W_ONE;
	obl->w_zero = W_ZERO;
	obl->w_zero_q16 = (65536/(2.0*PI))*W_ZERO;

	obl->dem_one[1] = obl->dem_one[0] = 0.0;
	obl->dem_one_q0[1] = obl->dem_one_q0[0] = 0.0;
	obl->dem_zero[1] = obl->dem_zero[0] = 0.0;
	obl->dem_zero_q0[1] = obl->dem_zero_q0[0] = 0.0;
	for(i=0; i<OBL_LPF; i++)
		obl->dem_lpf_in[i] = 0;
	obl->pdem_lpf_in = obl->dem_lpf_in;
	obl->dem_lpf = 0;
	
	for(i=0; i<OBL_LPF; i++)
		obl->dem_total_in[i] = 0;
	obl->pdem_total_in = obl->dem_total_in;
	obl->dem_total = 0;

	obl->dem_state = DEMOD_ST_WAIT_START;
	obl->dem_letters_figures = LETTERS;

	if (obl->autobaud_enabled)
	{
		obl->autobaud_state = AUTOBD_ST_WAIT_START;	
		obl->dem_baud_est = OBL_BAUD_INVALID;
		obl->dem_baud = DEM_BAUD;
	}
	else
	{
		obl->autobaud_state = AUTOBD_ST_DISABLED;	
		obl->dem_baud_est = baud;
		obl->dem_baud = baud;
	}
	
	

	/* init top level state machine */

	obl->top_state = TOP_STATE_RESET;
	obl->top_timer = 0;
}

void obl_set_speed(OBL *obl, int baud)
{
	assert(obl != NULL);
	assert((baud == OBL_BAUD_50) || (baud == OBL_BAUD_45) || (baud == OBL_BAUD_47));

	obl->mod_baud = baud;
	obl->mod_samples_per_bit = FS/obl->mod_baud;		

	/* recalculate number of stop bit samples if baud rate changes */

	obl_set_stop_bits(obl, obl->mod_num_stop_bits);
}

void obl_set_stop_bits(OBL *obl, int stop_bits)
{
	assert(obl != NULL);
	assert((stop_bits == OBL_STOP_1) || (stop_bits == OBL_STOP_1_5) || 
	       (stop_bits == OBL_STOP_2));

	obl->mod_num_stop_bits = stop_bits;
	obl->mod_num_stop_samples = stop_bits*obl->mod_samples_per_bit/2;
}

void obl_set_crlf(OBL *obl, int crlf) 
{
	assert(obl != NULL);
	assert((crlf == OBL_CRLF_ON) || (crlf == OBL_CRLF_OFF));

	obl->mod_crlf = crlf;
	obl->mod_chars_without_crlf = 0;
}
void obl_enable_autobaud(OBL *obl, int enabled)
{
	assert(obl != NULL);
	obl->autobaud_enabled = enabled;

}


void push_char(OBL *obl, char baudot)
{
	*(obl->pmod_buffer_in)++ = baudot;
	if (obl->pmod_buffer_in >= obl->mod_buffer+OBL_TEXT_BUF)
		obl->pmod_buffer_in = obl->mod_buffer;
	obl->mod_buffer_count++;
}

//#define DUMP_MOD_BUF
#ifdef DUMP_MOD_BUF
/* function used during development to dump mod_buf[] in human-readable ASCII */

static void dump_mod_buf(OBL *obl)
{
	int i;
	int mode = LETTERS;

	assert(obl != NULL);

	for(i=0; i<obl->mod_buffer_count; i++)
	{
		switch(obl->mod_buffer[i])
		{
		case LETR:
			printf("LETR\n");
			mode = LETTERS;
		break;
		case FIGR:
			printf("FIGR\n");
			mode = FIGURES;
		break;
                default:
			printf("[%d] 0x%02x %c\n", i, obl->mod_buffer[i], 
			       baudot_to_ascii[mode][(int)obl->mod_buffer[i]]);
		}
	}
	exit(0);	
}
#endif

/*
   TODO: modify to discard characters not in baudot character set.  Currently they
   are converted to spaces.

   TODO: fire off event when text transmission starts/stops, as per R4.1.4/4.1.5
*/
int obl_tx_queue(OBL *obl, const char* text)
{
	char ascii_char;  /*the current char to be transmitted*/
	char baudot_char; /*the current char as a baudot encoded char*/
	int increment;    /*the number of characters to increment to 
                       * next utf8 character in 'text'*/
	int i;		      /* a counter*/
	const char* ptext	= text; /* a pointer we use to walk 
	                            * through the chars*/
	int char_case		= OBL_NO_CASE;  /*the case of the character
	                                     * to be transmitted*/
	
	
	/*state that we are modulating*/
	obl->top_state = TOP_STATE_MOD;

	/*loop through each character in the text or until we have
	 reached our buffer size*/
	while ((*ptext != '\0') && obl->mod_buffer_count < OBL_TEXT_BUF)
	{
		/*retrieve the next character to process*/
		ascii_char = utf8_to_ascii(ptext,&increment);
		baudot_char = ascii_to_baudot[toupper(ascii_char)];

		/*this character can either be rendered as a FIGURES character,
		  a LETTERS character or WHITESPACE which can be rendered by
		  either character set*/
		if (ascii_char == ' ')
		{
			char_case = OBL_WHITESPACE;
		}
		else if ((ascii_char == 0x0d) || (ascii_char == 0x0a))
		{
			obl->mod_chars_without_crlf = 0;
			char_case = OBL_WHITESPACE;
		}
		else if (baudot_char & FIGFLAG)
		{
			char_case = OBL_FIGURES;
		}
		else
		{
			char_case = OBL_LETTERS;
		}

		
		/*see if we need to first insert a case character*/
		if (char_case == OBL_WHITESPACE)
		{
			/*we have white space, we don't need to insert
			 a letters/figures case character.  We will insert
			 a case character when we transition from white 
			 space to some other case.  The only exception to this
			 is when we are just starting to transmit, in which case
			 we must send something, so we'll choose the more common
			 LETTERS case*/
			if (obl->mod_case == OBL_NO_CASE)
				push_char(obl,LETR);

			obl->mod_case = OBL_WHITESPACE;
		}
		else if (char_case != obl->mod_case)
		{
			/*case is either LETTERS or FIGURES and doesn't match
			  our modulator's current state, so add a new case char
			  here*/
			push_char(obl, (char_case == OBL_LETTERS) ? LETR : FIGR);
			
			/*and save our current state*/
			obl->mod_case = char_case;
			
			/*and update our counter*/
			obl->mod_chars_without_case = 0;
		}

		/*we need to perform a little logic here to check for some
		 * boundry conditions in the protocol*/
		/* We need to ensure that we re-send the current case character
		   at least every 72 characters. */
		if (obl->mod_chars_without_case > 70) 
		{
			if (obl->mod_case == OBL_WHITESPACE)
				push_char(obl,LETR); /*doesn't matter which, so how 'bout LETR*/
			else
				push_char(obl, (obl->mod_case == OBL_LETTERS) ? LETR : FIGR);
			obl->mod_chars_without_case = 0;
		}

		/* we need to ensure that we send a carriage return/linefeed
		 * at least every 72 characters, but we start looking for a 
		 * space character starting at 60 characters */
		if (obl->mod_crlf == OBL_CRLF_OFF)
		{
			/*we just insert our regular character*/
			push_char(obl,baudot_char & 0x1f);
		}
		else if ((obl->mod_chars_without_crlf > 59) && 
			    (obl->mod_case == OBL_WHITESPACE))
		{
			/*insert the cr-lf instead of the space character*/
			push_char(obl,ascii_to_baudot[0x0d] & 0x1f);
			push_char(obl,ascii_to_baudot[0x0a] & 0x1f);
			obl->mod_chars_without_crlf = 0;
			push_char(obl,baudot_char & 0x1f);
		}
		else if (obl->mod_chars_without_crlf > 70)
		{
			/*insert the cr-lf followed by the character*/
			push_char(obl,ascii_to_baudot[0x0d] & 0x1f);
			push_char(obl,ascii_to_baudot[0x0a] & 0x1f);
			obl->mod_chars_without_crlf = 0;

			push_char(obl,baudot_char & 0x1f);
		}
		else
		{
			/*we just insert our regular character*/
			push_char(obl,baudot_char & 0x1f);
		}

		obl->mod_chars_without_case++;
		obl->mod_chars_without_crlf++;

		/*at this point, we've handled our character*/
		/*increment our pointer to the next character to transmit,
		 * We do this because UTF8 characters might be up to 
		 * 4 bytes long.*/
		for (i = 0; i < increment;i++)
		{
			ptext++;
			/*make sure we don't reach the end of the string*/
			if (*ptext == '\0')
				break;
		}
	}/* while (!done)*/
	
	/*we return the number of bytes read from the string*/
	return (int)(ptext - text);
}


/*
  TODO convert this to fixed point.  Determine sin with 8192 point
       LUT, use 0..8191 scaling for phase so it wraps around modulo
       2PI with simple bit-wise mask.

  TODO calibrate mod output to TIA spec level requirements R4.5.  This
       req references TIA-968-A which we don't have at present.
*/

#ifdef FIXED_PT
short INLINE modulate(OBL *obl, int bit)
{
	/* use of unsigned short in Q16 for format for phase
	   accumulator leads to natural wrap-around at 2PI
	   boundaries */

	if (bit) 
		obl->mod_phase_q16 += obl->w_one_q16;
	else 
		obl->mod_phase_q16 += obl->w_zero_q16;

	/* sin_lut_q15 is in Q15 format, therefore we rightshift 15 to
	   get Q0 at modulator output.  We cast multiply arguments
	   to 32 bit ints to ensure compiler uses a multiply with a 32
	   bit result.
	 */

	if (bit) 
		return ((int)obl->mod_one_amp*(int)sin_lut_q15[obl->mod_phase_q16>>2]) >> 15;
	else
		return ((int)obl->mod_zero_amp*(int)sin_lut_q15[obl->mod_phase_q16>>2]) >> 15;
}
#else
short INLINE modulate(OBL *obl, int bit)
{
	if (bit) 
		obl->mod_phase += obl->w_one;
	else 
		obl->mod_phase += obl->w_zero;

	/* modulo 2 PI */

	obl->mod_phase -= 2*PI*floor(obl->mod_phase/(2*PI));

	if (bit) 
		return obl->mod_one_amp*sin(obl->mod_phase);
	else
		return obl->mod_zero_amp*sin(obl->mod_phase);
}
#endif

/*
   TODO: transmit "1" hold tone for 150-300ms after last character
   sent as per R4.3 TIA spec.  Question, how do we know the last
   character has been sent?
*/

int obl_modulate(OBL *obl, short *buffer, int samples)
{
	int  i;
	int  non_idle_samples;
	int  next_state;

	assert(obl != NULL);
	assert(buffer != NULL);
	assert(samples >= 0);

	/* while we still have queued chars reset top level timer */
	if (obl->top_state != TOP_STATE_MOD)
	{
		return 0;
	}
	////if (obl->mod_buffer_count)
	if (obl->mod_state != MOD_ST_IDLE)
		obl->top_timer = 0;
	else
		obl->top_timer += samples;

	/* state machine to cycle thru bits.  One modulated sample is
	   generated for every cycle through the state machine. */

	non_idle_samples = 0;
	for(i=0; i<samples; i++) 
        {
		next_state = obl->mod_state;

		switch(obl->mod_state) 
                {
		case MOD_ST_IDLE:
			if (obl->mod_buffer_count) 
			{
				obl->mod_sample = 0;
				next_state = MOD_ST_START;
				(obl->callback)(obl,OBL_EVENT_TX_STATE, OBL_TRANSMIT_START);

				/* start moduling here to prevent lost sample */

				buffer[i] = modulate(obl, 0);
				non_idle_samples++;
				obl->mod_sample++;
			}	
		break;
		case MOD_ST_START:
			buffer[i] = modulate(obl, 0);
			non_idle_samples++;
			obl->mod_sample++;

			/* after sending start bit send LSB data bit */

			if (obl->mod_sample == obl->mod_samples_per_bit)
			{
				obl->mod_baudot = *obl->pmod_buffer_out++;
				if (obl->pmod_buffer_out >= obl->mod_buffer+OBL_TEXT_BUF)
					obl->pmod_buffer_out = obl->mod_buffer;
				obl->mod_buffer_count--;
				obl->mod_nbit = 0;
				obl->mod_bit = obl->mod_baudot & 0x1;

				obl->mod_sample = 0;
				next_state = MOD_ST_BIT;
			}
		break;
		case MOD_ST_BIT:
			buffer[i] = modulate(obl, obl->mod_bit);
			non_idle_samples++;
			obl->mod_sample++;

			/* when this data bit complete, send next data bit */

			if (obl->mod_sample == obl->mod_samples_per_bit)
			{
				obl->mod_sample = 0;
				obl->mod_nbit++;
				obl->mod_bit = (obl->mod_baudot >> obl->mod_nbit) & 0x1;
				if (obl->mod_nbit == NBITS) 
				{
					next_state = MOD_ST_STOP;
				}
			}
		break;
		case MOD_ST_STOP:
			buffer[i] = modulate(obl, 1);
			non_idle_samples++;
			obl->mod_sample++;

			/* when stop bit complete start transmission of next char,
			   of start sending hold tone if buffer empty */

			if (obl->mod_sample == obl->mod_num_stop_samples)
			{
				obl->mod_sample = 0;
				if (obl->mod_buffer_count) 
					next_state = MOD_ST_START;
				else
					next_state = MOD_ST_HOLD;
			}
		break;
		case MOD_ST_HOLD:
			buffer[i] = modulate(obl, 1);
			non_idle_samples++;
			obl->mod_sample++;
			if (obl->mod_buffer_count) ////
			{
				obl->mod_sample = 0;
				next_state = MOD_ST_START;
			}
			else if (obl->mod_sample == MOD_HOLD_SAM)
			{
				obl->mod_sample = 0;
				next_state = MOD_ST_IDLE;
				(obl->callback)(obl,OBL_EVENT_TX_STATE, OBL_TRANSMIT_STOP);
			}
		break;
		default:
			assert(0);
		}
		obl->mod_state = next_state;
	}

	return non_idle_samples;
}

void demod_dsp(OBL *obl, short sam)
{
#ifdef FIXED_PT
	short one_q0, zero_q0;
#else
	float one, zero;
#endif
	short x;

	/* perform DSP processing --------------------------------*/

	sam >>=5;

#ifdef FIXED_PT

	one_q0 =  sam;
	one_q0 += ((int)c1 * (int)obl->dem_one_q0[0]) >> 15;
	one_q0 -= ((int)c2 * (int)obl->dem_one_q0[1]) >> 15;
	obl->dem_one_q0[1] = obl->dem_one_q0[0];
	obl->dem_one_q0[0] = one_q0;
	
	zero_q0 =  sam;
	zero_q0 += ((int)c3 * (int)obl->dem_zero_q0[0]) >> 15;
	zero_q0 -= ((int)c2 * (int)obl->dem_zero_q0[1]) >> 15;
	obl->dem_zero_q0[1] = obl->dem_zero_q0[0];
	obl->dem_zero_q0[0] = zero_q0;
		
	x = abs(one_q0) - abs(zero_q0);
#else
	one = sam + 2.0*cos(W_ONE)*BETA*obl->dem_one[0] - 
		BETA*BETA*obl->dem_one[1];
	obl->dem_one[1] = obl->dem_one[0];
	obl->dem_one[0] = one;

	zero = sam + 2.0*cos(W_ZERO)*BETA*obl->dem_zero[0] - 
		BETA*BETA*obl->dem_zero[1];
	obl->dem_zero[1] = obl->dem_zero[0];
	obl->dem_zero[0] = zero;
	x = fabs(one) - fabs(zero);
#endif

	obl->dem_lpf -= (int)*obl->pdem_lpf_in;
	obl->dem_lpf += (int)x;
	*obl->pdem_lpf_in++ = (int)x;
	if (obl->pdem_lpf_in == obl->dem_lpf_in + OBL_LPF)
		obl->pdem_lpf_in = obl->dem_lpf_in;

	/* obl->dem_lpf is the output sample, ready for clock
	   recovery. */

	/* now work out total energy estimate, this is used to
	   determine if a valid signal is present by comparing
	   the modem tone to total energy ratio */

	x = abs(sam);
	obl->dem_total -= *obl->pdem_total_in;
	obl->dem_total += x;
	*obl->pdem_total_in++ = x;
	if (obl->pdem_total_in == obl->dem_total_in + OBL_LPF)
		obl->pdem_total_in = obl->dem_total_in;
}

/*
  TODO: post events for aborts like low signal drop out to assist in debug and
        testing with simulated and real world signals.
  TODO: work out a way to remove divides in demod processing (expensive in
        CPU cycles). 
*/


void obl_demodulate_packet(OBL *obl, char byte1, char byte2)
{
	short packet;
	char data[2];

	data[0] = byte1;
	data[1] = byte2;

	packet = (*(short *)data);

	obl_demodulate(obl, &packet, 1);
}

void obl_demodulate(OBL *obl, short *buffer, int samples)
{
	int   i,j;
	int   next_state;
	char  ascii;
	int   edge, dist, dist_50, dist_47, dist_45;
	int   dist_50_smooth, dist_47_smooth, dist_45_smooth;

	assert(obl != NULL);
	assert(buffer != NULL);
	assert(samples >= 0);

	/* iterate top level state machine */

	next_state = obl->top_state;

	switch(obl->top_state)
	{
        case TOP_STATE_RESET:
		next_state = TOP_STATE_DEMOD;
	break;
        case TOP_STATE_MOD:
		/* we disable demodulation during modulation, unless timer
		   has expired */

		if (obl->top_timer > TOP_TIMEOUT)
	        {
			/* Demod LETR/FIGR mode copied from modulator
			   mode, this ensures at tx/rx transition
			   demod has same mode as mod. */

			obl->dem_letters_figures = LETTERS;
			if (obl->mod_qstate == FIGURES)
				obl->dem_letters_figures = FIGURES;

			obl->top_timer = 0;

			/* fire off event indicating state transmission */

			(obl->callback)(obl,OBL_EVENT_TX_STATE, OBL_TRANSMIT_TIMEOUT);

			/* switch to demod mode */

			next_state = TOP_STATE_DEMOD;
		}
		else
			return;
	break;
        case TOP_STATE_DEMOD:
	break;
	}
	obl->top_state = next_state;

	/* main loop, process sample by sample */

	for(j=0; j<samples; j++) 
        {
		demod_dsp(obl, buffer[j]);

		next_state = obl->dem_state;

		switch(obl->dem_state) 
                {
		case DEMOD_ST_WAIT_START:
			/* 
			   The demodulator sits in this state when it
			   is waiting for a start bit.  When a FSK
			   "0" (1800 Hz) start-bit is detected that is
			   above a minimum threshold it transitions
			   out of this state.
			*/
			if (obl->dem_lpf < -MIN_THRESH*obl->dem_total && obl->dem_total != 0)
			{
				obl->dem_sample = FS/obl->dem_baud + FS/(2*obl->dem_baud);
				obl->dem_baudot = 0;
				obl->dem_bit = 0;
				next_state = DEMOD_ST_SAMPLE;
			}	
		break;
		case DEMOD_ST_SAMPLE:

			obl->dem_sample--;
			if (obl->dem_sample == 0)
			{
				if (obl->dem_lpf > 0)
					obl->dem_baudot |= (1 << obl->dem_bit);
				obl->dem_bit++;

				if (obl->dem_bit == NBITS)
				{

					/* complete baudot char has been received */

					switch(obl->dem_baudot)
					{
					case LETR: 
						obl->dem_letters_figures = LETTERS;
						(obl->callback)(obl,OBL_EVENT_DEMOD_CASE, OBL_LETTERS);
					break;	
					case FIGR:
						obl->dem_letters_figures = FIGURES;
						(obl->callback)(obl,OBL_EVENT_DEMOD_CASE, OBL_FIGURES);
					break;

					default:
						ascii = baudot_to_ascii[obl->dem_letters_figures][obl->dem_baudot];
						(obl->callback)(obl,OBL_EVENT_DEMOD_CHAR, ascii);
						obl->mod_case = OBL_WHITESPACE;////
					}

					obl->dem_sample = FS/obl->dem_baud;
					next_state = DEMOD_ST_WAIT_STOP;
				}
				else
				{
					/* sample next bit 1 bit-period later */
					obl->dem_sample = FS/obl->dem_baud;
				}
			}

			/* check for FSK signal drop out */

			if (abs(obl->dem_lpf) < MIN_THRESH*obl->dem_total || obl->dem_total == 0)
			{
				obl->dem_min_sig_timeout++;

				/* If signal drops out for approx one bit period
				   assume we have lost FSK signal */
				if (obl->dem_min_sig_timeout > (FS/OBL_BAUD_45))
				{
					next_state = DEMOD_ST_WAIT_START;
				}
			}
			else
				obl->dem_min_sig_timeout = 0;
		break;
		
		case DEMOD_ST_WAIT_STOP:

			/* wait until we are in the middle of stop bit */

			obl->dem_sample--;
			if (obl->dem_sample == 0)
			{
				next_state = DEMOD_ST_WAIT_START;
			}
		break;

		default:
			assert(0);
		}

		obl->dem_state = next_state;
		
		/* auto baud detection state machine ----------------------------*/

		/* 
		   Starts a counter at falling edge that marks the
		   beginning of the start bit.  Measures distance to
		   next rising edge.  Calculates the distance from
		   that rising edge to ideal position of the nearest
		   edge for (i) the 50 baud case and (ii) the 45 baud
		   case.  This gives us a distance metric for each
		   possible baud rate.  The distance measure is
		   smoothed over three samples.  The baud rate with
		   the smallest distance is chosen as the current baud
		   rate estimate.
		*/

		next_state = obl->autobaud_state;
		switch (obl->autobaud_state)
		{
		case AUTOBD_ST_WAIT_START:

			/* start bit kicks off this state machine */

			if (obl->dem_state == DEMOD_ST_SAMPLE)
			{
				next_state = AUTOBD_ST_WAIT_ZEROX;
				obl->autobaud_zerox = 0;
			}
		break;
		case AUTOBD_ST_WAIT_ZEROX:
			
			obl->autobaud_zerox++;
		        
			if (obl->dem_lpf > MIN_THRESH*obl->dem_total)
			{	
				/* OK, rising edge detected */

				/* determine distance to closest edge assuming 50 baud */

				dist_50 = FS/OBL_BAUD_50; 
				edge = 0;
				for(i=0; i<(NBITS+1); i++)
				{
					dist = abs(edge - obl->autobaud_zerox);
					if (dist < dist_50)
						dist_50 = dist;
					edge += FS/OBL_BAUD_50;
				}

				/* determine distance to closest edge assuming 47 baud */

				dist_47 = FS/OBL_BAUD_47; 
				edge = 0;
				for(i=0; i<(NBITS+1); i++)
				{
					dist = abs(edge - obl->autobaud_zerox);
					if (dist < dist_47)
						dist_47 = dist;
					edge += FS/OBL_BAUD_47;
				}

				/* determine distance to closest edge assuming 45 baud */

				dist_45 = FS/OBL_BAUD_45; 
				edge = 0;
				for(i=0; i<(NBITS+1); i++)
				{
					dist = abs(edge - obl->autobaud_zerox);
					if (dist < dist_45)
						dist_45 = dist;
					edge += FS/OBL_BAUD_45;
				}

				/* update 3-point smoothed distance estimates */

				dist_50_smooth = dist_50 + obl->dem_dist_50[0] + obl->dem_dist_50[1];
				obl->dem_dist_50[1] = obl->dem_dist_50[0];
				obl->dem_dist_50[0] = dist_50;

				dist_47_smooth = dist_47 + obl->dem_dist_47[0] + obl->dem_dist_47[1];
				obl->dem_dist_47[1] = obl->dem_dist_47[0];
				obl->dem_dist_47[0] = dist_47;

				dist_45_smooth = dist_45 + obl->dem_dist_45[0] + obl->dem_dist_45[1];
				obl->dem_dist_45[1] = obl->dem_dist_45[0];
				obl->dem_dist_45[0] = dist_45;
				
				/* make a decision on baud rate */

				if ((dist_50_smooth < dist_47_smooth) && (dist_50_smooth < dist_45_smooth))
					obl->dem_baud_est = OBL_BAUD_50;
				if ((dist_47_smooth < dist_50_smooth) && (dist_47_smooth < dist_45_smooth))
					obl->dem_baud_est = OBL_BAUD_47;
				if ((dist_45_smooth < dist_50_smooth) && (dist_45_smooth < dist_47_smooth))
					obl->dem_baud_est = OBL_BAUD_45;

				//printf("obl->autobaud_zerox: %d dist_50: %02d dist_47: %02d dist_45: %02d est: %d\n", 
				//       obl->autobaud_zerox, dist_50_smooth, dist_47_smooth, dist_45_smooth, obl->dem_baud_est);
				next_state = AUTOBD_ST_WAIT_STOP;
			}
			
			/* reset autobaud state machine, e.g. if FSK drops out before
			   we get a zero crossing */

			if (obl->dem_state != DEMOD_ST_SAMPLE)
			{
				next_state = AUTOBD_ST_WAIT_STOP;
			}

		break;
		case AUTOBD_ST_WAIT_STOP:
			if (obl->dem_state == DEMOD_ST_WAIT_STOP)
			{
				next_state = AUTOBD_ST_WAIT_START;
			}

		break;
		case AUTOBD_ST_DISABLED:
			break;
		default:
		       assert(0);
	
		}
		obl->autobaud_state = next_state;
		
	}

}

int obl_get_speed(OBL *obl)
{
	assert(obl != NULL);

	return obl->dem_baud_est;
}

short obl_get_amp(OBL *obl)
{
	assert(obl != NULL);

	return (obl->mod_one_amp + obl->mod_zero_amp)/2;
}

void obl_set_amp(OBL *obl, short amp)
{
	assert(obl != NULL);

	obl->mod_one_amp = amp;
	obl->mod_zero_amp = amp;
}

void obl_set_tx_freq(OBL *obl, float one_freq, float zero_freq)
{
	assert(obl != NULL);

	obl->w_one = (2*PI*one_freq/FS);
	obl->w_one_q16 = (65536/(2.0*PI))*obl->w_one;
	obl->w_zero = (2*PI*zero_freq/FS);
	obl->w_zero_q16 = (65536/(2.0*PI))*obl->w_zero;
}

void obl_set_tx_amplitude_imbalance(OBL *obl, short one_amp, short zero_amp)
{
	assert(obl != NULL);

	obl->mod_one_amp = one_amp;
	obl->mod_zero_amp = one_amp;
}
