#include "mp3_player.h"
#include "lib\helix\pub\mp3dec.h"
#include <stdio.h>
#include <string.h>
#include "fatfs.h"
#include "stm32746g_discovery_audio.h"
#include "term_io.h"
#include "dbgu.h"
#include "ansi.h"

//#define EOF 1

/* ------------------------------------------------------------------- */

static HMP3Decoder hMP3Decoder;
Mp3_Player_State state;

short output_buffer[AUDIO_OUT_BUFFER_SIZE]; // check if correct type and lenth?
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
uint8_t input_buffer[READ_BUFFER_SIZE];
static uint8_t *read_pointer;
static BSP_BUFFER_STATE out_buf_offs = BUFFER_OFFSET_NONE;
FIL input_file;
static int bytes_left=0;
static int bytes_left_before_decoding=0;
static int in_buf_offs;
static int underflows;
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
			    FILINFO info;
				if (f_readdir(&directory, &info) != FR_OK) {
                    xprintf("Error reading from directory");
                    return;
				}
                if (info.fname[0] == 0) {
                    f_closedir(&directory);
                    f_opendir(&directory, path);
                    continue;
                }
                if (strstr(info.fname, ".mp3")) {
                    if (f_open(&input_file, info.fname, FA_READ) != FR_OK) {
                        xprintf("Error opening file");
                        return;
                    }
                    state = PLAY;
                }
				break;
			case FINISH:
				return;
            default:
                return;
		}
	}

}

void BSP_init(void) {
	if(BSP_AUDIO_OUT_Init(OUTPUT_DEVICE_BOTH, 60, AUDIO_FREQUENCY_44K) == 0)	// probably will only need to init once?
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
	// open file from disk (get it from fileset)
	// Check if is valid Mp3 file
	// Call process_frame once, enable DMA transfer to AUDIO_OUT
	// In loop: listern for ts input - make appropirate actions (next, stop etc)
	// LOOp will break when all data has been read from file
	// Disable DMA transer to audio_out
	// close file and free decoder object

	hMP3Decoder = MP3InitDecoder();

//	if(checktype(fileset->current) == unsported) {
//		return NEXT;
//	}
//
//	if(f_open(&input_file, fileset->current, FA_READ) != FR_OK) {
//		return FINISH;
//	}

	if(mp3_player_process_frame() == 0) {
		state = PLAY;
		BSP_AUDIO_OUT_Play((uint16_t*)&output_buffer[0], AUDIO_OUT_BUFFER_SIZE);
		while(1) {
			if(state != PLAY) {
				break;
			}
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
}

// Process mp3 file
int mp3_player_process_frame(void)
{
	MP3FrameInfo mp3FrameInfo;

	if (read_pointer == NULL) {
		if(fill_input_buffer() != 0){
			return EOF;
		}
	}

	in_buf_offs = MP3FindSyncWord(read_pointer, bytes_left);
	while(in_buf_offs < 0) {
		if(fill_input_buffer() != 0)
			return EOF;
		if(bytes_left > 0){
			bytes_left -= 1;
			read_pointer += 1;
		}
		in_buf_offs = MP3FindSyncWord(read_pointer, bytes_left);
	}
	read_pointer += in_buf_offs;
	bytes_left -= in_buf_offs;
	bytes_left_before_decoding = bytes_left;

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

	//if(out_buf_offs == BUFFER_OFFSET_NONE)
	//{ // OR FULL?
	//	underflows++;
	//}

	// IF LOW EMPTY
	if(out_buf_offs == BUFFER_OFFSET_HALF)
	{
		decode_result = MP3Decode(hMP3Decoder, &read_pointer, &bytes_left, output_buffer, 0);
		out_buf_offs = BUFFER_OFFSET_NONE;
	}
	if(out_buf_offs == BUFFER_OFFSET_FULL){
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
			//do nothing, next call to MP3Decode will provide more data
			break;
		default:
			return 0; //skip this frame if error
			//return END_OF_FILE; //skip this file if error
		}
	}
	return 0;
}

// Callback when half of audio out buffer is transefered
void BSP_AUDIO_OUT_TransferComplete_CallBack(void)
{
    out_buf_offs = BUFFER_OFFSET_FULL;

	if(mp3_player_process_frame() != 0) // zwroci EOF gdy skonczy sie plik
	{
		BSP_AUDIO_OUT_Stop(CODEC_PDWN_SW);
		state = NEXT;
	}
}

// Callback when all of audio out buffer is transefered
void BSP_AUDIO_OUT_HalfTransfer_CallBack(void)
{
    out_buf_offs = BUFFER_OFFSET_HALF;

	if(mp3_player_process_frame() != 0) // zwroci EOF gdy skonczy sie plik
	{
		BSP_AUDIO_OUT_Stop(CODEC_PDWN_SW);
		state = NEXT;
	}
}

// Fill the input buffer with mp3 data from the USB for the decoder
int fill_input_buffer() {

	unsigned int bytes_read;
	unsigned int bytes_to_read;

	if(bytes_left > 0) {
		memcpy(input_buffer, read_pointer, bytes_left);
	}

	bytes_to_read = READ_BUFFER_SIZE - bytes_left;

	f_read(&input_file, (BYTE *)input_buffer + bytes_left, bytes_to_read, &bytes_read);

	if (bytes_read == bytes_to_read){
		read_pointer = input_buffer;
		in_buf_offs = 0;
		bytes_left = READ_BUFFER_SIZE;
		return 0;
	}
	else{
		return EOF;
	}

}
