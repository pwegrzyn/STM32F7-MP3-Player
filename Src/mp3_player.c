/*
 *	Main MP3 Player functionality
 */

#include <stdio.h>
#include <string.h>

#include "mp3_player.h"
#include "gui.h"
#include "lib\helix\pub\mp3dec.h"
#include "fatfs.h"
#include "stm32746g_discovery_audio.h"
#include "term_io.h"
#include "dbgu.h"
#include "ansi.h"

#define DEBUG_ON 0

// Data read from the USB and fed to the MP3 Decoder
#define READ_BUFFER_SIZE 2 * MAINBUF_SIZE// + 216

// Decoded data ready to be passed out output
#define DECODED_MP3_FRAME_SIZE MAX_NGRAN * MAX_NCHAN * MAX_NSAMP
#define AUDIO_OUT_BUFFER_SIZE 2 * DECODED_MP3_FRAME_SIZE

// State of the offset of the BSP Out buffer
typedef enum BSP_BUFFER_STATE_TAG {
    BUFFER_OFFSET_NONE = 0,
    BUFFER_OFFSET_HALF,
    BUFFER_OFFSET_FULL,
} BSP_BUFFER_STATE;

/* ------------------------------------------------------------------- */

static HMP3Decoder hMP3Decoder;
Mp3_Player_State state;
short output_buffer[AUDIO_OUT_BUFFER_SIZE];
uint8_t input_buffer[READ_BUFFER_SIZE];
static uint8_t *read_pointer;
static BSP_BUFFER_STATE out_buf_offs = BUFFER_OFFSET_NONE;
FIL input_file;
static int bytes_left=0;
static int in_buf_offs;
static int decode_result;

/* ------------------------------------------------------------------- */
void BSP_init(void);
void mp3_player_init(void);
void mp3_player_fsm(const char*);
void mp3_player_play();
int mp3_player_process_frame();
int fill_input_buffer();

/* ------------------------------------------------------------------- */

// Main Finite State Machine of the player
void mp3_player_fsm(const char* path)
{
    BSP_init();
    state = NEXT;

    DIR directory;
    f_opendir(&directory, path);

	while(1)
	{
		switch(state)
		{
			case PLAY:
				mp3_player_play();
                f_close(&input_file);
				break;
			case NEXT:
			    ;
			    if(DEBUG_ON) xprintf("fsm: state -> next\n");
			    FILINFO info;
				if (f_readdir(&directory, &info) != FR_OK) {
                    xprintf("Error reading from directory\n");
                    return;
				}
                if (info.fname[0] == 0) {
                    xprintf("fsm: no more files to read\n");
                    f_closedir(&directory);
                    f_opendir(&directory, path);
                    continue;
                }
                if (strstr(info.fname, ".mp3")) {
                    if (f_open(&input_file, info.fname, FA_READ) != FR_OK) {
                        xprintf("Error opening file\n");
                        return;
                    }
                    if(DEBUG_ON) xprintf("fsm: state next -> play\n");
                    state = PLAY;
                }
				break;
			case FINISH:
			    if(DEBUG_ON) xprintf("fsm: state -> finish\n");
				return;
            default:
                if(DEBUG_ON) xprintf("fsm: state -> default\n");
                return;
		}
	}

}

// Initialize AUDIO_OUT
void BSP_init(void) {
	if(BSP_AUDIO_OUT_Init(OUTPUT_DEVICE_BOTH, 100, AUDIO_FREQUENCY_44K) == 0)
	{
	  BSP_AUDIO_OUT_SetAudioFrameSlot(CODEC_AUDIOFRAME_SLOT_02);
		xprintf("Audio Init Ok\n");
	}
	else
	{
		xprintf("Audio Init Error\n");
	}
}

// Play state handler
void mp3_player_play(void)
{
	if(DEBUG_ON) xprintf("play: initializing decoder\n");
	hMP3Decoder = MP3InitDecoder();

    if(DEBUG_ON) xprintf("play: starting frame processing\n");
	if(mp3_player_process_frame() == 0) {
		state = PLAY;
		BSP_AUDIO_OUT_Play((uint16_t*)&output_buffer[0], AUDIO_OUT_BUFFER_SIZE * 2);
		touchscreen_loop_init();
		while(1) {
			Mp3_Player_State mewState = check_touchscreen();
			if (newState != EMPTY)
				state = newState;
		}
		BSP_AUDIO_OUT_Stop(CODEC_PDWN_SW);
		out_buf_offs = BUFFER_OFFSET_NONE;
	}
	else
	{
		state = NEXT;
	}

	bytes_left = 0;
	read_pointer = NULL;
	MP3FreeDecoder(hMP3Decoder);
	if(DEBUG_ON) xprintf("play: freeing decoder\n");
}

// Process next mp3 frame from the main file
int mp3_player_process_frame(void)
{
    if(DEBUG_ON) xprintf("process: starting\n");
	MP3FrameInfo mp3FrameInfo;

	if (read_pointer == NULL) 
	{
		if(fill_input_buffer() != 0)
			return EOF;
	}

	in_buf_offs = MP3FindSyncWord(read_pointer, bytes_left);
	while(in_buf_offs < 0) 
	{
		if(fill_input_buffer() != 0) return EOF;
		if(bytes_left > 0)
		{
			bytes_left--;
			read_pointer++;
		}
		in_buf_offs = MP3FindSyncWord(read_pointer, bytes_left);
	}
	read_pointer += in_buf_offs;
	bytes_left -= in_buf_offs;
	if(DEBUG_ON) xprintf("process: findSyncWord read\n");

	if (MP3GetNextFrameInfo(hMP3Decoder, &mp3FrameInfo, read_pointer) == 0 &&
			mp3FrameInfo.nChans == 2 &&
			mp3FrameInfo.version == 0)
	{
	}
	else
	{
		if(bytes_left > 0)
		{
			bytes_left -= 1;
			read_pointer += 1;
		}
		return 0;
	}

	if (bytes_left < MAINBUF_SIZE)
	{
		if(fill_input_buffer() != 0)
			return EOF;
	}

	if(out_buf_offs == BUFFER_OFFSET_HALF)
	{
	    if(DEBUG_ON) xprintf("process: out_buf half\n");
		decode_result = MP3Decode(hMP3Decoder, &read_pointer, &bytes_left, output_buffer, 0);
		out_buf_offs = BUFFER_OFFSET_NONE;
	}
	if(out_buf_offs == BUFFER_OFFSET_FULL){
        if(DEBUG_ON) xprintf("process: out_buf full\n");
		decode_result = MP3Decode(hMP3Decoder, &read_pointer, &bytes_left, &output_buffer[DECODED_MP3_FRAME_SIZE], 0);
		out_buf_offs = BUFFER_OFFSET_NONE;
	}

	if(decode_result != ERR_MP3_NONE)
	{
		switch(decode_result)
		{
		case ERR_MP3_INDATA_UNDERFLOW:
			bytes_left = 0;
			if(fill_input_buffer() != 0)
				return EOF;
			break;
		case ERR_MP3_MAINDATA_UNDERFLOW:
			break;
		default:
			return 0;
		}
	}
	return 0;
}

// Callback when half of audio out buffer is transefered
void BSP_AUDIO_OUT_TransferComplete_CallBack(void)
{
    out_buf_offs = BUFFER_OFFSET_FULL;

	if(mp3_player_process_frame() != 0)
	{
		BSP_AUDIO_OUT_Stop(CODEC_PDWN_SW);
		state = NEXT;
	}
}

// Callback when all of audio out buffer is transefered
void BSP_AUDIO_OUT_HalfTransfer_CallBack(void)
{
    out_buf_offs = BUFFER_OFFSET_HALF;

	if(mp3_player_process_frame() != 0)
	{
		BSP_AUDIO_OUT_Stop(CODEC_PDWN_SW);
		state = NEXT;
	}
}

// Fill the input buffer with mp3 data from the USB for the decoder
int fill_input_buffer() {

	unsigned int bytes_read;
	unsigned int bytes_to_read;

	if(bytes_left > 0) 
	{
		memcpy(input_buffer, read_pointer, bytes_left);
	}

	if(DEBUG_ON) xprintf("fill: copied from read_pointer input_buffer\n");

	bytes_to_read = READ_BUFFER_SIZE - bytes_left;

	f_read(&input_file, (BYTE *)input_buffer + bytes_left, bytes_to_read, &bytes_read);

	if(DEBUG_ON) xprintf("fill: copied from file to input_buffer\n");

	if (bytes_read == bytes_to_read)
	{
		read_pointer = input_buffer;
		in_buf_offs = 0;
		bytes_left = READ_BUFFER_SIZE;
		return 0;
	}
	else
	{
		return EOF;
	}
}
