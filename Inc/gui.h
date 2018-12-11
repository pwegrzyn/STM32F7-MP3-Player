#ifndef GUI_H_
#define GUI_H_

// Call first 3 functions in main after debug_init()
void lcd_start(void);
void draw_background(void);
int initialize_touchscreen(void);

// Read TS input and react (single iteration)
Mp3_Player_State check_touchscreen(void);

// Prepare TS for check_touchscreen() series
void touchscreen_loop_init(void);

#endif