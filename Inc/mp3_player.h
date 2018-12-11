#ifndef MP3_PLAYER_H_
#define MP3_PLAYER_H_

void mp3_player_fsm(const char*);

// Represents player states (FSM)
typedef enum Mp3_Player_State_Tag {
    PLAY,
    PAUSE,
    STOP,
    NEXT,
    PREV,
    FINISH,
	EMPTY
} Mp3_Player_State;

#endif