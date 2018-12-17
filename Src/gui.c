/*
 *	Graphical User Interface
 *  Touchscreen
 */

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
uint16_t buttonsLeftUpper[4][2] = { {XPix(0.08), YPix(0.7)}, {XPix(0.31), YPix(0.7)}, {XPix(0.54), YPix(0.7)}, {XPix(0.77), YPix(0.7)} };
Mp3_Player_State buttonState[4] = { PREV, PLAY, STOP, NEXT };
Mp3_Player_State playButtonState = PLAY;
uint32_t lastTicks = 0;

/* ------------------------------------------------------------------- */

void lcd_start(void);
void LCD_Start_v2(void);
void draw_background(void);
int initialize_touchscreen(void);
void touchscreen_loop_init(void);
Mp3_Player_State check_touchscreen(void);
uint16_t getXPix (double factor);
uint16_t getYPix (double factor);

/* ------------------------------------------------------------------- */

// Initialize the LCD display
void lcd_start(void)
{
  /* LCD Initialization */
  BSP_LCD_Init();

  /* LCD Initialization */
  BSP_LCD_LayerDefaultInit(LAYER_BG, (unsigned int)0xC0000000);
  //BSP_LCD_LayerDefaultInit(1, (unsigned int)lcd_image_bg+(LCD_X_SIZE*LCD_Y_SIZE*4));
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

// We can go with this instead probably
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
void draw_background(void)
{
	/* Select the LCD Background Layer  */
	BSP_LCD_SelectLayer(LAYER_BG);

	BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
	BSP_LCD_FillRect(buttonsLeftUpper[0][0], buttonsLeftUpper[0][1],  CONTROL_BUTTON_SIZE, CONTROL_BUTTON_SIZE);
	BSP_LCD_FillRect(buttonsLeftUpper[1][0], buttonsLeftUpper[1][1],  CONTROL_BUTTON_SIZE, CONTROL_BUTTON_SIZE);
	BSP_LCD_FillRect(buttonsLeftUpper[2][0], buttonsLeftUpper[2][1],  CONTROL_BUTTON_SIZE, CONTROL_BUTTON_SIZE);
	BSP_LCD_FillRect(buttonsLeftUpper[3][0], buttonsLeftUpper[3][1],  CONTROL_BUTTON_SIZE, CONTROL_BUTTON_SIZE);

	//select Foreground Layer
	BSP_LCD_SelectLayer(LAYER_FG);
}

// Initialize the touchscreen
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
Mp3_Player_State check_touchscreen(void)
{
	uint32_t currentTicks = HAL_GetTick();
	if (currentTicks < lastTicks + TICKS_DELTA)
		return EMPTY;


    BSP_TS_GetState(&TS_State);
	BSP_LCD_Clear(LCD_COLOR_WHITE);
	if ((TS_State.touchX[0] & 0x0FFF) >= 40)
    {
		newX = TS_State.touchX[0] & 0x0FFF;
	}
	if ((TS_State.touchY[0] & 0x0FFF) >= 40)
    {
		newY = TS_State.touchY[0] & 0x0FFF;
	}

	if (lastState.touchX[0] == newX && lastState.touchY[0] == newY)
		return EMPTY;

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

		if (i == 1)
			if (playButtonState == PLAY) {
                playButtonState = PAUSE;
                return PAUSE;
			}

			else {
                playButtonState = PLAY;
                return PLAY;
			}

		else
			return buttonState[i];
		}
	}

	return EMPTY;

	// vTaskDelay(10);
}

uint16_t getXPix (double factor)  {
	return factor * LCD_X_SIZE;
}

uint16_t getYPix (double factor)  {
	return factor * LCD_Y_SIZE;
}
