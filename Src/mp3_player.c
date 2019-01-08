/*
 *	Main MP3 Player functionality
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mp3_player.h"
#include "gui.h"
#include "lib\helix\pub\mp3dec.h"
#include "fatfs.h"
#include "stm32746g_discovery_audio.h"
#include "term_io.h"
#include "dbgu.h"
#include "ansi.h"

#define DEBUG_ON 1

// Data read from the USB and fed to the MP3 Decoder
#define READ_BUFFER_SIZE 2 * MAINBUF_SIZE// + 216

// Max size of a single frame
#define DECODED_MP3_FRAME_SIZE MAX_NGRAN * MAX_NCHAN * MAX_NSAMP

// Decoded data ready to be passed out to output (always have space to hold 2 frames)
#define AUDIO_OUT_BUFFER_SIZE 2 * DECODED_MP3_FRAME_SIZE

// State of the offset of the BSP output buffer
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
static uint8_t *current_ptr;
static BSP_BUFFER_STATE out_buf_offs = BUFFER_OFFSET_NONE;
FIL input_file;
char** paths;
int mp3FilesCounter = 0;
int currentFilePosition = -1;
int currentFileBytes = 0;
int currentFileBytesRead = 0;
static int buffer_leftover = 0;
static int in_buf_offs;
static int decode_result;
int has_been_paused = 0;
char gui_info_text[100];

/* ------------------------------------------------------------------- */

void BSP_init(void);
void mp3_player_init(void);
void mp3_player_fsm(const char*);
void mp3_player_play();
int mp3_player_process_frame();
int fill_input_buffer();
void copy_leftover();
void reset_player_state();

/* ------------------------------------------------------------------- */

// Main Finite State Machine of the player
void mp3_player_fsm(const char* path)
{
    BSP_init();
    state = NEXT;

    touchscreen_loop_init();

	sprintf(gui_info_text, "Initializing...");
	refresh_screen(gui_info_text);

    DIR directory;
    FILINFO info;

    if (f_opendir(&directory, path) != FR_OK) {
        if (DEBUG_ON) xprintf("Error opening the directory\n");
        return;
    }

    while(1) {
        if (f_readdir(&directory, &info) != FR_OK) {
            xprintf("Error reading from directory\n");
            return;
        }
        if (info.fname[0] == 0)
            break;
        if (strstr(info.fname, ".mp3"))
            mp3FilesCounter++;
    }

    f_closedir(&directory);

    int i = 0;
    paths = malloc(sizeof(char*) * mp3FilesCounter);

    if (paths == NULL) {
        if (DEBUG_ON) xprintf("Error allocating memory\n");
        return;
    }

    if (f_opendir(&directory, path) != FR_OK) {
        if (DEBUG_ON) xprintf("Error opening the directory\n");
        return;
    }

    while(1) {
        if (f_readdir(&directory, &info) != FR_OK) {
            xprintf("Error reading from directory\n");
            return;
        }
        if (info.fname[0] == 0)
            break;
        if (strstr(info.fname, ".mp3")) {
            paths[i] = malloc((strlen(info.fname) + 1) * sizeof(char));
			strcpy(paths[i], info.fname);
			if(DEBUG_ON) xprintf("%s\n", paths[i]);
            i++;
        }
    }

	f_closedir(&directory);

	while(1)
	{	
		switch(state)
		{
			case PLAY:
			    if(DEBUG_ON) xprintf("Now playing\n");
				if (f_findfirst(&directory, &info, path, paths[currentFilePosition]) != FR_OK) {
            		xprintf("Error looking for first file occurence\n");
            		return;
        		} 
				currentFileBytes = info.fsize;
				f_closedir(&directory);
				sprintf(gui_info_text, "%s", paths[currentFilePosition]);
				refresh_screen(gui_info_text);
				mp3_player_play();
                f_close(&input_file);
				currentFileBytesRead = 0;
				break;
			case NEXT:
			    reset_player_state();
			    if (currentFilePosition == mp3FilesCounter - 1)
                    currentFilePosition = 0;
                else
                    currentFilePosition++;
                if (f_open(&input_file, paths[currentFilePosition], FA_READ) != FR_OK) {
                    if(DEBUG_ON) xprintf("Error opening file\n");
                    return;
                }
                state = PLAY;
				break;
            case PREV:
                reset_player_state();
                if (currentFilePosition == 0)
                    currentFilePosition = mp3FilesCounter - 1;
                else
                    currentFilePosition--;
                if (f_open(&input_file, paths[currentFilePosition], FA_READ) != FR_OK) {
                    if(DEBUG_ON) xprintf("Error opening file\n");
                    return;
                }
                state = PLAY;
                break;
            case PAUSE:
				// shouldn't ever come to this place
                break;
            case STOP:
				update_progress_bar(0);
                reset_player_state();
                currentFilePosition = 0;
				sprintf(gui_info_text, "STOPPED");
				refresh_screen(gui_info_text);
                while(state == STOP) {
                    Mp3_Player_State newState = check_touchscreen();
                    if (newState != EMPTY)
                        state = newState;
                }
                if (state == PLAY)
                    if (f_open(&input_file, paths[currentFilePosition], FA_READ) != FR_OK) {
                        if(DEBUG_ON) xprintf("Error opening file\n");
                        return;
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
		while(1) {
			update_progress_bar(((double)currentFileBytesRead) / currentFileBytes);
			Mp3_Player_State newState = check_touchscreen();
			if (newState != EMPTY)
				state = newState;
            if (!has_been_paused && state == PAUSE) {
				sprintf(gui_info_text, "PAUSED");
				refresh_screen(gui_info_text);
				if(BSP_AUDIO_OUT_Pause() != AUDIO_OK) {
					xprintf("Error while pausing stream\n");
					return;
				}
				has_been_paused = 1;
            } else if(has_been_paused && state == PLAY) {
				sprintf(gui_info_text, "%s", paths[currentFilePosition]);
				refresh_screen(gui_info_text);
				if(BSP_AUDIO_OUT_Resume() != AUDIO_OK) {
					xprintf("Error while pausing stream\n");
					return;
				}
				has_been_paused = 0;
			}  else if (has_been_paused && state == PAUSE)
				continue;
			else if (state != PLAY) {
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

	buffer_leftover = 0;
	current_ptr = NULL;
	MP3FreeDecoder(hMP3Decoder);

	if(DEBUG_ON) xprintf("play: freeing decoder\n");
}

// Process next mp3 frame from the main file
int mp3_player_process_frame(void)
{
    if(DEBUG_ON) xprintf("process: starting\n");
	MP3FrameInfo mp3FrameInfo;

	if (current_ptr == NULL && fill_input_buffer() != 0) return EOF;

	in_buf_offs = MP3FindSyncWord(current_ptr, buffer_leftover);

	while(in_buf_offs < 0)
	{
		if(fill_input_buffer() != 0) return EOF;
		// TODO check if this if is necessary at all
		if(buffer_leftover > 0)
		{
			buffer_leftover--;
			current_ptr++;
		}
		// END TODO
		in_buf_offs = MP3FindSyncWord(current_ptr, buffer_leftover);
	}
	
	current_ptr += in_buf_offs;
	buffer_leftover -= in_buf_offs;

	if(DEBUG_ON) xprintf("process: findSyncWord read\n");

	// get data from the frame header
	if(!(MP3GetNextFrameInfo(hMP3Decoder, &mp3FrameInfo, current_ptr) == 0 && mp3FrameInfo.nChans == 2 && mp3FrameInfo.version == 0)) 
	{
		// if header has failed
		if(buffer_leftover > 0)
		{
			buffer_leftover--;
			current_ptr++;
		}
		return 0;
	}

	// if feel the buffer with actual non-frame-header data if necessary
	if (buffer_leftover < MAINBUF_SIZE && fill_input_buffer() != 0) return EOF;

	// decode the right portion of the buffer
	if(out_buf_offs == BUFFER_OFFSET_HALF)
	{
	    if(DEBUG_ON) xprintf("process: out_buf half\n");
		decode_result = MP3Decode(hMP3Decoder, &current_ptr, &buffer_leftover, output_buffer, 0);
		out_buf_offs = BUFFER_OFFSET_NONE;
	}

	if(out_buf_offs == BUFFER_OFFSET_FULL)
	{
        if(DEBUG_ON) xprintf("process: out_buf full\n");
		decode_result = MP3Decode(hMP3Decoder, &current_ptr, &buffer_leftover, &output_buffer[DECODED_MP3_FRAME_SIZE], 0);
		out_buf_offs = BUFFER_OFFSET_NONE;
	}

	// check results of the decoding process
	if(decode_result != ERR_MP3_NONE)
	{
		if(decode_result == ERR_MP3_INDATA_UNDERFLOW)
		{
			buffer_leftover = 0;
			if(fill_input_buffer() != 0) return EOF;
		}
		else if(decode_result == ERR_UNKNOWN)
		{
			xprintf("An unkown error has occured while decoding the frame\n");
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
int fill_input_buffer() 
{
	unsigned int actually_read, how_much_to_read;

	copy_leftover();

	if(DEBUG_ON) xprintf("fill: copied from current_ptr input_buffer\n");

	how_much_to_read = READ_BUFFER_SIZE - buffer_leftover;

	// read from the input_file to fill the input_buffer fully
	f_read(&input_file, (BYTE *)input_buffer + buffer_leftover, how_much_to_read, &actually_read);

	currentFileBytesRead += actually_read;

	if(DEBUG_ON) xprintf("fill: copied from file to input_buffer\n");

	// if there's still data in  the file
	if (actually_read == how_much_to_read)
	{
		current_ptr = input_buffer;
		in_buf_offs = 0;
		buffer_leftover = READ_BUFFER_SIZE;
		return 0;
	}
	else return EOF;
}

// if there is some data left in the buffer copy it to the beginning
void copy_leftover() 
{
	if(buffer_leftover > 0)
	{
		memcpy(input_buffer, current_ptr, buffer_leftover);
	}
}

// reset all the used data structures
void reset_player_state() 
{
	buffer_leftover = 0;
    current_ptr = NULL;
    out_buf_offs = BUFFER_OFFSET_NONE;
}