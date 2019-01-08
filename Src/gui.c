/*
 *	Graphical User Interface
 *  Touchscreen
 */

/* Includes ------------------------------------------------------------------*/
#include <stdio.h>
#include <string.h>
#include "mp3_player.h"
#include "stm32746g_discovery_lcd.h"
#include "Utilities/Fonts/fonts.h"
#include "stm32746g_discovery_ts.h"
#include "term_io.h"
#include "dbgu.h"
#include "ansi.h"
#include "stm32f7xx_hal.h"

#define LCD_X_SIZE			RK043FN48H_WIDTH
#define LCD_Y_SIZE			RK043FN48H_HEIGHT
#define LAYER_FG 1
#define LAYER_BG 0

#define CONTROL_BUTTON_SIZE 0.15 * LCD_X_SIZE
#define CONTROL_BUTTONS_NUMBER 4
#define TICKS_DELTA 100

#define XPix(x) x * LCD_X_SIZE
#define YPix(x) x * LCD_Y_SIZE

// Not sure about these two guys (may be used in LCD_START_V2() in LayerDefaultInit()?)
// static uint32_t lcd_image_fg[LCD_Y_SIZE][LCD_X_SIZE] __attribute__((section(".sdram"))) __attribute__((unused));
// static uint32_t lcd_image_bg[LCD_Y_SIZE][LCD_X_SIZE] __attribute__((section(".sdram"))) __attribute__((unused));

/* ------------------------------------------------------------------- */

static TS_StateTypeDef TS_State;
static TS_StateTypeDef lastState;
uint16_t newX;
uint16_t newY;
uint16_t buttonsLeftUpper[4][2] = { 
	{XPix(0.08), YPix(0.7)}, 
	{XPix(0.31), YPix(0.7)}, 
	{XPix(0.54), YPix(0.7)}, 
	{XPix(0.77), YPix(0.7)} };
Mp3_Player_State buttonState[4] = { PREV, PLAY, STOP, NEXT };
Mp3_Player_State playButtonState = PLAY;
uint32_t lastTicks = 0;

/* ------------------------------------------------------------------- */

void lcd_start(void);
void LCD_Start_v2(void);
void draw_background(void);
int initialize_touchscreen(void);
void touchscreen_loop_init(void);
Mp3_Player_State check_touchscreen();
void update_progress_bar(double);
uint16_t getXPix (double factor);
uint16_t getYPix (double factor);
void refresh_screen(const char *info_text);
void draw_buttons(void);
void update_play_pause_button(void);

/* ------------------------------------------------------------------- */

// Initialize the LCD display (call this only once at the start of the player!)
void lcd_start(void)
{
  /* LCD Initialization */
  BSP_LCD_Init();

  /* LCD Initialization */
  BSP_LCD_LayerDefaultInit(LAYER_BG, (unsigned int)0xC0000000);
  BSP_LCD_LayerDefaultInit(LAYER_FG, (unsigned int)0xC0000000+(LCD_X_SIZE*LCD_Y_SIZE*4));

  /* Enable the LCD */
  BSP_LCD_DisplayOn();

  /* Select the LCD Background Layer  */
  BSP_LCD_SelectLayer(LAYER_BG);

  /* Clear the Background Layer */
  BSP_LCD_Clear(LCD_COLOR_WHITE);
  BSP_LCD_SetBackColor(LCD_COLOR_WHITE);

  BSP_LCD_SetColorKeying(LAYER_FG,LCD_COLOR_WHITE);

  /* Select the LCD Foreground Layer  */
  BSP_LCD_SelectLayer(LAYER_FG);

  /* Clear the Foreground Layer */
  BSP_LCD_Clear(LCD_COLOR_WHITE);
  BSP_LCD_SetBackColor(LCD_COLOR_WHITE);

  /* Configure the transparency for foreground and background :
     Increase the transparency */
  BSP_LCD_SetTransparency(LAYER_BG, 255);
  BSP_LCD_SetTransparency(LAYER_FG, 255);
}

// Alternative LCD start-up method
void LCD_Start_v2(void)
{
	BSP_LCD_Init();
	BSP_LCD_LayerDefaultInit(LAYER_FG, (unsigned int)0xC0000000);
	BSP_LCD_LayerDefaultInit(LAYER_BG, (unsigned int)0xC0000000+(LCD_X_SIZE*LCD_Y_SIZE*4));

	BSP_LCD_SelectLayer(LAYER_FG);
	BSP_LCD_Clear(LCD_COLOR_WHITE);
	BSP_LCD_SelectLayer(LAYER_BG);
	BSP_LCD_Clear(LCD_COLOR_WHITE);
	BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
	BSP_LCD_SetTextColor(LCD_COLOR_BLACK);

	BSP_LCD_ResetColorKeying(LAYER_FG);
	BSP_LCD_ResetColorKeying(LAYER_BG);

	BSP_LCD_SetTransparency(LAYER_FG, (uint8_t) 100);
	BSP_LCD_SetTransparency(LAYER_BG, (uint8_t) 0xFF);

	BSP_LCD_Reload(LCD_RELOAD_VERTICAL_BLANKING);
	BSP_LCD_DisplayOn();
}

// Draw the screen background
// Should be called only once to draw all the necessary screen elements
void draw_background(void)
{
	/* Select the LCD Background Layer  */
	BSP_LCD_SelectLayer(LAYER_BG);

	draw_buttons();
	
	//select Foreground Layer
	BSP_LCD_SelectLayer(LAYER_FG);
}

// Initialize the touchscreen
// Should be called once, to create all the necessary structures
int initialize_touchscreen(void)
{
	uint8_t status = 0;
	status = BSP_TS_Init(BSP_LCD_GetXSize(), BSP_LCD_GetYSize());
	if(status != TS_OK) return -1;
	return 0;
}

// Call this once to init the TS-input-reading-loop
void touchscreen_loop_init(void)
{
    newX = 120;
	newY = 120;
    BSP_LCD_SelectLayer(LAYER_FG);
	BSP_LCD_SetTextColor(LCD_COLOR_GREEN);
	BSP_LCD_SetTransparency(LAYER_FG, (uint8_t) 0xFF);
	BSP_LCD_SetColorKeying(LAYER_FG, LCD_COLOR_WHITE);
}

// Single iteration of getting TS input
// RETURNS: new state according to the user choice or EMPTY if no new choice has been made
Mp3_Player_State check_touchscreen()
{	
	uint32_t currentTicks = HAL_GetTick();
	
	if (currentTicks < lastTicks + TICKS_DELTA)
		return EMPTY;
	
	lastTicks = currentTicks;
	
    BSP_TS_GetState(&TS_State);
	if (TS_State.touchDetected == 0)
		return EMPTY;
	if ((TS_State.touchX[0] & 0x0FFF) >= 40)
    {
		newX = TS_State.touchX[0] & 0x0FFF;
	}
	if ((TS_State.touchY[0] & 0x0FFF) >= 40)
    {
		newY = TS_State.touchY[0] & 0x0FFF;
	}

	lastState.touchX[0] = newX;
	lastState.touchY[0] = newY;

	for(int i = 0; i < CONTROL_BUTTONS_NUMBER; i++) {

		uint16_t buttonCornerX = buttonsLeftUpper[i][0];
		uint16_t buttonCornerY = buttonsLeftUpper[i][1];

		if (newX <= buttonCornerX + CONTROL_BUTTON_SIZE &&
			newY <= buttonCornerY + CONTROL_BUTTON_SIZE &&
			newX > buttonCornerX &&
			newY > buttonCornerY
		) {

		if (i == 1 || i == 2) {
			if (playButtonState == PLAY) {
                playButtonState = PAUSE;
			}

			else {
                playButtonState = PLAY;
			}
		}
		if (i == 1)
			return playButtonState;
		else if (i == 2) {
			playButtonState = PAUSE;
			return STOP;
		}
		
		else
			return buttonState[i];
		}
	}

	return EMPTY;
}

// Update the visual progress bar for the current song
// INPUT: progress - floating point value of 0 to 1 indicating the part of the whole song which has already been played
void update_progress_bar(double progress) {

	BSP_LCD_SelectLayer(LAYER_FG);
	
	double epsilon = 1e-2;
	if(progress <= epsilon) {
		BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
		BSP_LCD_FillRect(0, YPix(0.45) - 1, LCD_X_SIZE, 21);
		return;
	}

	BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
	BSP_LCD_FillRect(30, YPix(0.45),  (uint16_t)(progress * (LCD_X_SIZE - 60)), 19);

}

// Refresh the state of the screen
// INPUT: info_text - text field which should be displayed on the main title-bar of the player
void refresh_screen(const char *info_text) {

	// Text
	BSP_LCD_SelectLayer(LAYER_FG);
	BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
	BSP_LCD_FillRect(0, YPix(0.20) - 1, LCD_X_SIZE, 30);
	BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
	// Workaround for a small visual bug (the first letter not showing up in displayStringAt())
	char buf[100];
	sprintf(buf, " %s", info_text);
	BSP_LCD_DisplayStringAt(XPix(0.05), YPix(0.20), (unsigned char *)buf,LEFT_MODE);

	// Play/Pause Button (update label)
	update_play_pause_button();

}

// Draw the 4 main state control buttons: PREV/PLAY_PAUSE/STOP/NEXT
void draw_buttons() {

	//BSP_LCD_SetBackColor(LCD_COLOR_RED);
	BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
	BSP_LCD_FillRect(buttonsLeftUpper[0][0], buttonsLeftUpper[0][1],  CONTROL_BUTTON_SIZE, CONTROL_BUTTON_SIZE);
	BSP_LCD_FillRect(buttonsLeftUpper[1][0], buttonsLeftUpper[1][1],  CONTROL_BUTTON_SIZE, CONTROL_BUTTON_SIZE);
	BSP_LCD_FillRect(buttonsLeftUpper[2][0], buttonsLeftUpper[2][1],  CONTROL_BUTTON_SIZE, CONTROL_BUTTON_SIZE);
	BSP_LCD_FillRect(buttonsLeftUpper[3][0], buttonsLeftUpper[3][1],  CONTROL_BUTTON_SIZE, CONTROL_BUTTON_SIZE);

	BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
	uint16_t xButton, yButton;
	
	// Previous button label
	xButton = buttonsLeftUpper[0][0];
	yButton = buttonsLeftUpper[0][1];
	Point Points1[]= {{xButton + 7, yButton + CONTROL_BUTTON_SIZE / 2}, {xButton + CONTROL_BUTTON_SIZE / 2, yButton + 7}, 
		{xButton + CONTROL_BUTTON_SIZE / 2, yButton + 7 + 58}};	
	// Control button size in pixels is 72
	BSP_LCD_FillPolygon(Points1, 3);
	Points1[0].X += 29;
	Points1[1].X += 29;
	Points1[2].X += 29;
	BSP_LCD_FillPolygon(Points1, 3);

	// Play/Pause button label
	update_play_pause_button();

	BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
	// Stop button label
	xButton = buttonsLeftUpper[2][0];
	yButton = buttonsLeftUpper[2][1];
	BSP_LCD_FillRect(xButton + 15, yButton + 15, 42, 42);

	// Next button label
	xButton = buttonsLeftUpper[3][0];
	yButton = buttonsLeftUpper[3][1];
	Point Points3[]= {{xButton + 7, yButton + 7}, {xButton + 7, yButton + 7 + 58}, 
		{xButton + CONTROL_BUTTON_SIZE / 2 , yButton + CONTROL_BUTTON_SIZE / 2}};
	BSP_LCD_FillPolygon(Points3, 3);
	Points3[0].X += 29;
	Points3[1].X += 29;
	Points3[2].X += 29;
	BSP_LCD_FillPolygon(Points3, 3);

}

// Refresh only the PLAY/PAUSE button
void update_play_pause_button() {

	BSP_LCD_SelectLayer(LAYER_BG);
	uint16_t xButton = buttonsLeftUpper[1][0];
	uint16_t yButton = buttonsLeftUpper[1][1];
	BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
	BSP_LCD_FillRect(xButton, yButton,  CONTROL_BUTTON_SIZE, CONTROL_BUTTON_SIZE);
	BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
	if(playButtonState == PAUSE) {
		Point Points2[]= {{xButton + 7, yButton + 7}, {xButton + 7, yButton + 7 + 58}, 
			{xButton + 7 + 58, yButton + CONTROL_BUTTON_SIZE / 2}};
		BSP_LCD_FillPolygon(Points2, 3);
	} else if(playButtonState == PLAY) {
		// Workaround for a small visual bug (first pause-label figure not showing up)
		BSP_LCD_FillRect(0, 0, 1, 1);
		BSP_LCD_FillRect(xButton + 7, yButton + 7, 27, CONTROL_BUTTON_SIZE - 14);
		BSP_LCD_FillRect(xButton + 7 + 31, yButton + 7, 27, CONTROL_BUTTON_SIZE - 14);
	}

}

// Auxillary functions
uint16_t getXPix (double factor)  {
	return factor * LCD_X_SIZE;
}

uint16_t getYPix (double factor)  {
	return factor * LCD_Y_SIZE;
}
