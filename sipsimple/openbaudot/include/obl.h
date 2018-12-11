/** @file */
/*  
 * OBL openbaudot Library
 * 
 * Copyright (C) 2007-2008 Board of Regents of the University of Wisconsin 
 *              System (Univ. of Wisconsin-Madison, Trace R&D Center)
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
 * file...: obl.h
 * original author.: David Rowe
 * Created: 27 August 2007
 * 
 */

#ifndef __OBL__
#define __OBL__

#ifdef __cplusplus
extern "C" {
#endif

#define OBL_BAUD_50       50
#define OBL_BAUD_47       47
#define OBL_BAUD_45       45
#define OBL_BAUD_INVALID 100

#define OBL_CRLF_ON        1
#define OBL_CRLF_OFF       0

#define OBL_TEXT_BUF    1024

#define OBL_EVENT_DEMOD_CHAR  1 
#define OBL_EVENT_DEMOD_ABORT 2
#define OBL_EVENT_DEMOD_CASE  4   /* inform application regarding a 
							      * demodulated LETRS/FIGURES case 
								  * character*/
#define OBL_EVENT_TX_STATE    3   /* data value is 1 for transmitting 
                                     or 0 for not transmitting,
									 and 2 for timeout (following transmit)*/
#define OBL_TRANSMIT_STOP     0
#define OBL_TRANSMIT_START    1
#define OBL_TRANSMIT_TIMEOUT  2



#define OBL_STOP_1         2        /* 1 stop bit    */
#define OBL_STOP_1_5       3        /* 1.5 stop bits */
#define OBL_STOP_2         4        /* 2 stop bits   */

#define OBL_LPF            20        /* Low pass filter order */


#define OBL_LETTERS        1       /* letters mode/state            */
#define OBL_FIGURES        2       /* figures mode/state            */
#define OBL_WHITESPACE     3       /* whitespace, (either letters/figs) */
#define OBL_NO_CASE  	   4       /* a non-initialized state */

/**
* @brief OBL state variables, one created for each modem instance
*/

typedef struct {
	/** callback function used to post events */
	int  (*callback)(void* obl, int event, int data);

	/* modulator state variables */

	char   mod_buffer[OBL_TEXT_BUF]; /**< stores baudot codes after pre 
                                            processing by obl_tx_queue()     */
	char  *pmod_buffer_in;       /**< mod_buffer[] input pointer         */
	char  *pmod_buffer_out;      /**< mod_buffer[] output pointer        */
	int    mod_buffer_count;     /**< no. used bytes in mod_buffer[]     */
	int    mod_qstate;           /**< current state of queuing state 
                                               machine */
	int    mod_case;  /**< current letters/figures state      */
	int    mod_chars_without_crlf; /**< number of chars we have buffered
					  without inserting a CR-LF          */
	int	   mod_chars_without_case;
	int    mod_return_state;     /**< used to return to a previous state */

	int    mod_baud;             /**< modulator baud rate                */
	int    mod_samples_per_bit;  /**< number of samples for one bit      */

	int    mod_state;      /**< current state of obl_modulate state mach  */
	int    mod_baudot;     /**< current baudot char being modulated       */
	int    mod_nbit;       /**< num bits modulated so far in this char    */
	int    mod_bit;        /**< current bit in mod_baudot being modulated */
	int    mod_sample;     /**< current sample in bit being modulated     */
	float  mod_phase;      /**< current phase value (phase accumulator)   */
        unsigned 
        short mod_phase_q16;   /**< phase acc 0..2pi -> 0..65536              */
        int    mod_one_amp;    /**< peak modulator amplitude for one tone     */
	int    mod_zero_amp;   /**< peak modulator amplitude for zero tone    */
	float  w_one;          /**< one freq in normalised rads               */
	float  w_zero;         /**< zero freq in normalised rads              */
        unsigned 
        short w_one_q16;       /**< scaled one freq 0..2pi -> 0..65536        */
        short w_zero_q16;      /**< scaled zero freq 0..2pi -> 0..65536       */

	/* various mode switches */

	int    mod_num_stop_bits;    /**< number of stop bits                 */
	int    mod_num_stop_samples; /**< number of samples for stop bit      */
	int    mod_crlf;             /**< non-zero for CRLF mode              */

	/* demodulator state variables */
	
	float  dem_one[2];     /**< previous 2 output samples of one filter   */
	short  dem_one_q0[2];  /**< previous 2 output samples of one filter   */
	float  dem_zero[2];    /**< previous 2 output samples of zero filter  */
	short  dem_zero_q0[2]; /**< previous 2 output samples of one filter   */
	short  dem_lpf_in[OBL_LPF]; /**< previous 6 LPF input samples         */  
	short *pdem_lpf_in;         /**< ptr to oldest LPF input sample       */
	int    dem_lpf;             /**< current LPF output value             */
	short  dem_total_in[OBL_LPF]; /**< previous 6 energy input samples    */  
	short *pdem_total_in;       /**< ptr to oldest energy sample          */
	int    dem_total;           /**< current energy output value          */
	float  dem_ratio;           /**< ratio of LPF output/energy           */
	int    dem_state;           /**< current demod state machine state    */
	int    dem_min_sig_timeout; /**< minimum signal time out counter      */
	int    dem_baud;            /**< demod baud rate                      */
	int    dem_baudot;          /**< current baudot code being received   */
	int    dem_bit;             /**< current bit being received           */
        int    dem_sample;          /**< down counter for sampling instant    */
	int    dem_letters_figures; /**< current letters/figures mode         */

	int	   autobaud_enabled;    /**< audio baud detection enable/disable flag */
	int    autobaud_state;      /**< auto buad detection state mach state */
	int    autobaud_zerox;      /**< samples to zero crossing             */
        int    dem_dist_50[2];      /**< previous 50 baud distance measures   */
        int    dem_dist_47[2];      /**< previous 47 baud distance measures   */
        int    dem_dist_45[2];      /**< previous 45 baud distance measures   */
	int    dem_baud_est;        /**< autobaud estimate                    */

	/* top level state machine */

	int    top_state;           /**< top level state machine state        */
	int    top_timer;           /**< timer nased on tx samples processed  */
	void*  user_data;	   /**< a place for a user to keep data 
				     * such as channel info */
} OBL;

/**
* Initialise an instance of an obl modem.  Should be called once to
* initialise the modem.
*
* @param obl the instance of the current modem
* @param baud the initial baud rate of the modem
* @param callback the callback function for this instance
* @return void
*/
void obl_init(OBL *obl, int baud, int (*callback)(void* obl, int event, int data));


/**
* Re-initialise an instance of an obl modem.  Can be called at any time to
* re-initialise the modem.
*
* @param obl the instance of the current modem
* @param baud the initial baud rate of the modem
* @return void
*/
void obl_reset(OBL *obl, int baud);


/**
* Sets the modulation rate to either OBL_BAUD_50 or or OBL_BAUD_47 or
* OBL_BAUD_45. The default is OBL_BAUD_50.  The demodulation rate is
* detected automatically and can be determined by the obl_get_speed
* function.
*
* @param obl the instance of the current modem
* @param baud either OBL_BAUD_50 or OBL_BAUD_47 or OBL_BAUD_45
* @return void
*/
void obl_set_speed(OBL *obl, int baud);


/**
* Controls the CR-LF insertion mode. In OBL_CRLF_ON mode, CR-LF pairs are
* automatically inserted every 72 characters in the modulated data.  The default
* is OBL_CRLF_ON.
*
* @param obl the instance of the current modem
* @param crlf either OBL_CRLF_ON or OBL_CRLF_OFF
* @return void
*/
void obl_set_crlf(OBL *obl, int crlf);


/**
* Controls the number of stop bits the modulator inserts.  The default
* (and value recommended by the TIA standard is 1.5).  Other values
* are useful for testing the demodulator, as baudot units in the field
* vary widely.
*
* @param obl the instance of the current modem
* @param stop_bits either OBL_STOP_1 or OBL_STOP_1_5 or OBL_STOP_2
* @return void
*/
void obl_set_stop_bits(OBL *obl, int stop_bits);

/**
* Queue characters to be transmitted by the FSK modulator.  The maximum 
* queue size if set by OBL_MOD_QUEUE.
*
* @param obj the instance of the current modem
* @param text the text to be queued
* @return the number of characters which were queued,
*         which may be less than strlen(text) if the buffer is full.
*/
int obl_tx_queue(OBL *obl, const char* text);


/**
* Fills audio buffers with modulated FSK samples, or silence
* if there is no text queued.  The return value can be used to determine
* when modulation is complete.
*
* @param obj the instance of the current modem
* @param buffer buffer of FSK samples
* @param samples the number of samples in buffer
* @return the number of samples containing non-silence FSK
*/
int obl_modulate(OBL *obl, short *buffer, int samples);


/**
* Converts FSK samples to characters.  The demodulated characters are
* returned via the call back function.  The call back function is also
* used to return error and status information.
*
* It is OK to pass short arrays (less than one character or even one
* bit) to this function.  The demodulator will partially process the
* data and output a text character when enough data is accumulated.
*
* @param obj the instance of the current modem
* @param buffer buffer of FSK samples
* @param samples the number of samples in buffer
* @return void
*/
void obl_demodulate(OBL *obl, short *buffer, int samples);


/**
* Returns the current demodulator speed, which is autodetected by the
* demodulator shortly after demodulation begins.  The OBL_BAUD_INVALID
* indicates that the demodulator has not made a decision, for example
* if FSK data is not present.
*
* @param obj the instance of the current modem
* @return either OBL_BAUD_50 or OBL_BAUD_47 or OBL_BAUD_45 or OBL_BAUD_INVALID
*/
int obl_get_speed(OBL *obl);

/**
* Gets the current modulator peak amplitude.
*
* @param obj the instance of the current modem
* @return modulator peak amplitude
*/
short obl_get_amp(OBL *obl);

/**
* Sets the modulator peak amplitude in the range -32767 to 32767.
*
* @param obj the instance of the current modem
* @param amp the new modulator peak amplitude
*/
void obl_set_amp(OBL *obl, short amp);

/**
* Sets the modulator one and zero frequencies, used for testing
* demodulator ability to handle frequency offsets.  Not used
* in normal modem operation.
*
* @param obj the instance of the current modem
* @param one_freq the frequency of the one in Hz (default 1400)
* @param zero_freq the frequency of the zero in Hz (default 1800)
* @return void
*/
void obl_set_tx_freq(OBL *obl, float one_freq, float zero_freq);

/**
* Sets the modulator one and zero amplitudes, used for testing
* demodulator ability to handle amplitude imbalance.  Not used
* in normal modem operation.
*
* @param obj the instance of the current modem
* @param one_amp the peak amplitude of the one tone
* @param zero_amp the peak amplitude of the zero tone
* @return void
*/
void obl_set_tx_amplitude_imbalance(OBL *obl, short one_amp, short zero_amp);


/**
* enables or disables baud-rate matching for demodulation.  
* 
* @param obj the instance of the current modem
* @param enabled set to 0 to disable, 1 to enable
* @return void
*/
void obl_enable_autobaud(OBL *obl, int enabled);

#ifdef __cplusplus
}
#endif


#endif

