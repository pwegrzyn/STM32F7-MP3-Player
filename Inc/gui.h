#ifndef GUI_H_
#define GUI_H_

// Call first 3 functions in main after debug_init()
void lcd_start(void);
void draw_background(void);
int initialize_touchscreen(void);

// Read TS input and react (single iteration)
Mp3_Player_State check_touchscreen();

// Prepare TS for check_touchscreen() series
void touchscreen_loop_init();

// Refresh the state of the screen
void refresh_screen(const char *info_text);

// Update the visual song progress bar
void update_progress_bar(double);

#endif