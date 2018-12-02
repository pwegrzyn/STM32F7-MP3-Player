#ifndef MP3_PLAYER_H_
#define MP3_PLAYER_H_

// Data read from the USB and fed to the MP3 Decoder
#define READ_BUFFER_SIZE 2 * MAINBUF_SIZE// + 216

// Decoded data ready to be passed out output
#define DECODED_MP3_FRAME_SIZE MAX_NGRAN * MAX_NCHAN * MAX_NSAMP
#define AUDIO_OUT_BUFFER_SIZE 2 * DECODED_MP3_FRAME_SIZE

void mp3_player_fsm(FIL* file);

// Represents player states (FSM)
typedef enum Mp3_Player_State_Tag {
    PLAY,
    PAUSE,
    STOP,
    NEXT,
    PREV,
    FINISH
} Mp3_Player_State;

// Represents file list with files to decode and play
typedef struct Mp3_Player_Filset_Tag {
    int files_count;
} Mp3_Player_Fileset;

// State of the offset of the BSP Out buffer
typedef enum BSP_BUFFER_STATE_TAG {
    BUFFER_OFFSET_NONE = 0,  
    BUFFER_OFFSET_HALF,  
    BUFFER_OFFSET_FULL,     
} BSP_BUFFER_STATE;


#endif