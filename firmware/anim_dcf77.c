#include <stdbool.h>
#include <stdint.h>
#include <avr/pgmspace.h>
#include "anim_dcf77.h"
#include "glcd.h"
#include "ks0108.h"
#include "ratt.h"

#ifdef DCF77INFOSCREEN

#ifndef DCF77ENABLE
#error DCF77ENABLE must be set to display DCF infoscreen
#endif

// global variables for time
extern volatile uint8_t time_s, time_m, time_h;

// static part of the screen-content
// drawn once on initialization
char DCF_info_text[] PROGMEM = "     DCF77 Info\0"
                               "\0"
                               " Now:      :  :\0"
                               " Status: wait\0"
                               " Last:\0"
                               "\0"
                               "Z000    S0000    O000";

// used to perform decimal-conversions of vales
static const uint16_t DecimalValues[] = {1000, 100, 10, 1};

// the seconds that are shown on the screen
// A redraw of the time-line and the last-line is needed, if this value differs from
// the global seconds-value time_s.
uint8_t time_s_displayed;

// two values indicating that the status-line has to be redrawn
bool draw_dcf_status_fail, draw_dcf_status_read;

// last-valid-signal-counter (counts seconds since last valid DCF signal)
uint16_t last_signal_s;

// duration of the last received ones, zeros, start-pulses an error-pulses
uint16_t dcf_zero_duration, dcf_one_duration, dcf_start_duration, dcf_errorbyte_duration;

// true, if a redraw of the duration-line is needed
bool duration_line_changed;

// contains the last 21 received raw bytes of the DCF signal (+ '\0'-termination)
char dcf_byte_line[22];

// true, if a redraw of the raw byte line is needed
bool dcf_byte_line_changed;



// draws/updates the line on the infoscreen, that shows the time
void dcfinfo_draw_timeline(void);

// draws/updates the line on the infoscreen, that displays a "last-valid-signal-counter"
void dcfinfo_draw_lastline(void);

// draws/updates the infoscreen-line, that shows the duration of the last received ones, zeros, start-pulses an error-pulses
void dcfinfo_draw_durationline(void);

// updates saved raw bytes of the DCF signal
// c is the last received byte
// c = '0' -> logical zero
// c = '1' -> logical one
// c = 'S' -> start of signal indication
// c = 'F' -> pulse of invalid length
void dcfinfo_update_byteline(char c);

// draws/updates the last line of the infoscreen, where the raw DCF bytes are shown
void dcfinfo_draw_byteline(void);

// prints a decimal-value with a given number of digits on the infoscreen
// Values are printed with leading zeroes, if necessary.
// Works with max. 4 digits.
void glcd_put_dec(uint16_t value, uint8_t digits);

// prints a decimal-value without leading zeros
void glcd_put_dec2(uint16_t value);



void initanim_dcfinfo(void) {
  glcdClearScreen();

  // draw the static parts of the infoscreen
  uint8_t display_line_adress = 0;
  for(uint8_t display_line = 0; display_line < 7; display_line++) {
    glcdSetAddress(0, display_line);
    glcdPutStr_rom(&DCF_info_text[display_line_adress], NORMAL);
    while(pgm_read_byte(&DCF_info_text[display_line_adress++]));
  }

  // initialize variables
  for(uint8_t i = 0; i < 21; i++) {
    dcf_byte_line[i] = ' ';
  }
  time_s_displayed = 255;
  last_signal_s = UINT16_MAX;
  dcf_zero_duration = 0;
  dcf_one_duration = 0;
  dcf_start_duration = 0;
  dcf_errorbyte_duration = 0;
  duration_line_changed = false;
  dcf_byte_line[21] = '\0';
  dcf_byte_line_changed = false;
  draw_dcf_status_fail = false;
  draw_dcf_status_read = false;
}



void step_dcfinfo(void) {
  if(time_s_displayed != time_s) {
    dcfinfo_draw_timeline();
    dcfinfo_draw_lastline();
    time_s_displayed = time_s;
  }

  // update status-line
  if(draw_dcf_status_read) {
    glcdGotoChar(3, 9);
    glcdPutStr_rom(PSTR("read"), false);
    draw_dcf_status_read = false;
  }
  if(draw_dcf_status_fail) {
    glcdGotoChar(3, 9);
    glcdPutStr_rom(PSTR("fail"), false);
    draw_dcf_status_fail = false;
  }

  if(duration_line_changed) {
    dcfinfo_draw_durationline();
    duration_line_changed = false;
  }

  if(dcf_byte_line_changed) {
    dcfinfo_draw_byteline();
    dcf_byte_line_changed = false;
  }
}



void dcfinfo_status_read() {
  // paint on next redraw-step
  draw_dcf_status_read = true;
}



void dcfinfo_status_fail() {
  // paint on next redraw-step
  draw_dcf_status_fail = true;
}



void dcfinfo_reset_last() {
  // reset counter
  last_signal_s = 0;
  // force line on infoscreen to be redrawn
  time_s_displayed = 255;
}



void dcfinfo_zero_received(uint16_t duration_ms) {
  // save duration and redraw, if necessary
  if(dcf_zero_duration != duration_ms) {
    dcf_zero_duration = duration_ms;
    duration_line_changed = true;
  }
  // update raw-info
  dcfinfo_update_byteline('0');
}



void dcfinfo_one_received(uint16_t duration_ms) {
  // save duration and redraw, if necessary
  if(dcf_one_duration != duration_ms) {
    dcf_one_duration = duration_ms;
    duration_line_changed = true;
  }
  // update raw-info
  dcfinfo_update_byteline('1');
}



void dcfinfo_start_received(uint16_t duration_ms) {
  // save duration and redraw, if necessary
  if(dcf_start_duration != duration_ms) {
    dcf_start_duration = duration_ms;
    dcf_errorbyte_duration = 0;
    duration_line_changed = true;
  }
  // update raw-info
  dcfinfo_update_byteline('S');
}



void dcfinfo_errorbyte_received(uint16_t duration_ms) {
  // save duration and redraw, if necessary
  if(dcf_errorbyte_duration != duration_ms) {
    dcf_errorbyte_duration = duration_ms;
    dcf_start_duration = 0;
    duration_line_changed = true;
  }
  // update raw-info
  dcfinfo_update_byteline('F');
}



void dcfinfo_draw_timeline() {
  glcdGotoChar(2, 9);
  glcd_put_dec(time_h, 2); // hour
  glcdGotoChar(2, 12);
  glcd_put_dec(time_m, 2); // minute
  glcdGotoChar(2, 15);
  glcd_put_dec(time_s, 2); // seconds
}



void dcfinfo_draw_lastline() {
  if(time_s_displayed != 255) { // 255 means no time drawn yet

    // calculate the seconds passed by since the last redraw
    // Usually the difference is one second (ds=1)
    int8_t ds = (int8_t)time_s - (int8_t)time_s_displayed;
    if(ds < 0) { // next minute
      ds += 60;
    }

    // add the difference to the counter using a saturated addition
    if(UINT16_MAX - last_signal_s > ds) {
      last_signal_s += ds;
    } else {
      last_signal_s = UINT16_MAX;
    }
  }

  // draw the counter
  glcdGotoChar(4, 9);
  if(last_signal_s != UINT16_MAX) {
    if(last_signal_s < 60) {
      // draw seconds
      glcd_put_dec2(last_signal_s);
      glcdWriteChar('s', false);
    } else if(last_signal_s < 3600) {
      // draw minutes
      glcd_put_dec2(last_signal_s / 60);
      glcdWriteChar('m', false);
    } else {
      // draw hours
      glcd_put_dec2(last_signal_s / 3600);
      glcdWriteChar('h', false);
    }
    glcdPutStr_rom(PSTR(" ago "), false);
  } else {
    // receipt of last valid signal is too long ago
    glcdPutStr_rom(PSTR("never  "), false);
  }
}



void dcfinfo_draw_durationline(void) {
  glcdGotoChar(6, 1);
  glcd_put_dec(dcf_zero_duration, 3);
  glcdGotoChar(6, 8);
  // only one of errorbyte-duration or start-duration can be displayed
  if(dcf_errorbyte_duration) {
    glcdWriteChar('F', false);
    glcd_put_dec(dcf_errorbyte_duration, 4);
  } else {
    glcdWriteChar('S', false);
    glcd_put_dec(dcf_start_duration, 4);
  }
  glcdGotoChar(6, 18);
  glcd_put_dec(dcf_one_duration, 3);
}



void dcfinfo_update_byteline(char c) {
  // shift all older raw bytes one position to the left
  // the oldest gets lost
  for(uint8_t i = 0; i < 20; i++) {
    dcf_byte_line[i] = dcf_byte_line[i+1];
  }
  // save the newest
  dcf_byte_line[20] = c;
  // the line has to be redrawn
  dcf_byte_line_changed = true;
}



void dcfinfo_draw_byteline(void) {
  glcdSetAddress(0, 7);
  // dcf_byte_line is allways terminated with a '\0'!
  glcdPutStr_ram(dcf_byte_line, false);
}



void glcd_put_dec(uint16_t number, uint8_t digits) {
  for(uint8_t digit = 0; digit < 4; digit++) {
    if((4-digit) <= digits) {
      char c = (char)((number/DecimalValues[digit])%10);
      c += '0';
      glcdWriteChar(c, false);
    }
  }
}



void glcd_put_dec2(uint16_t number) {
  if(number < 10) {
    glcd_put_dec(number, 1);
  } else if(number < 100) {
    glcd_put_dec(number, 2);
  } else if(number < 1000) {
    glcd_put_dec(number, 3);
  } else {
    glcd_put_dec(number, 4);
  }
}

#endif
