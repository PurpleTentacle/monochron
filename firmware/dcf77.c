#include <stdbool.h>
#include <avr/eeprom.h>
#include "anim_dcf77.h"
#include "dcf77.h"
#include "ratt.h"

#ifdef DCF77ENABLE

// global variables for time and date
extern volatile uint8_t time_s, time_m, time_h;
extern volatile uint8_t date_m, date_d, date_y;

// state of DCF-pin when last read. Used to recognize state changes
volatile uint8_t dcf_pin_state_save;

// amount of milliseconds the DCF-pin remains in an unchanged state
volatile uint16_t dcf_pin_ms_count;

// state of DCF-pin before state-change
volatile uint8_t last_dcf_pin_state;

// amount of milliseconds the DCF-pin remained in an unchanged state before state-change
// Set to zero once the information is evaluated
volatile uint16_t last_dcf_pin_ms;

// time and date received from DCF
uint8_t dcf_minute, dcf_hour, dcf_day, dcf_month, dcf_year;

// the parity of the received values
bool dcf_parity;

// the received time is send to the RTC-chip on the next rising edge if this value is true
bool dcf_commit_time;

// one DCF-state for each second in a minute
enum DcfState {
  WAIT_START,
  CUSTOM_BIT0,  CUSTOM_BIT1,  CUSTOM_BIT2,  CUSTOM_BIT3,  CUSTOM_BIT4,  CUSTOM_BIT5,
  CUSTOM_BIT6,  CUSTOM_BIT7,  CUSTOM_BIT8,  CUSTOM_BIT9,  CUSTOM_BIT10, CUSTOM_BIT11,
  CUSTOM_BIT12, CUSTOM_BIT13, CUSTOM_BIT14,
  RBIT, TIMESHIFT, SUMMERTIME, WINTERTIME, LEAPSECOND,
  STARTBIT,
  MINUTE_BIT0, MINUTE_BIT1, MINUTE_BIT2, MINUTE_BIT3, MINUTE_BIT4, MINUTE_BIT5, MINUTE_BIT6,
  MINUTE_PARITY,
  HOUR_BIT0, HOUR_BIT1, HOUR_BIT2, HOUR_BIT3, HOUR_BIT4, HOUR_BIT5,
  HOUR_PARITY,
  DAY_BIT0, DAY_BIT1, DAY_BIT2, DAY_BIT3, DAY_BIT4, DAY_BIT5,
  DAYOFWEEK_BIT0, DAYOFWEEK_BIT1, DAYOFWEEK_BIT2,
  MONTH_BIT0, MONTH_BIT1, MONTH_BIT2, MONTH_BIT3, MONTH_BIT4,
  YEAR_BIT0, YEAR_BIT1, YEAR_BIT2, YEAR_BIT3, YEAR_BIT4, YEAR_BIT5, YEAR_BIT6, YEAR_BIT7,
  DATE_PARITY
};
enum DcfState dcf_state;

// time and date are coded decimal
static const uint8_t DcfDecimalBitValues[] = {1, 2, 4, 8, 10, 20, 40, 80};



// resets all variables. After calling this function the module is waiting for a new DCF-signal to start
void dcf_reset(void);

// called when the DCF-pin was in high-state for a given duration
void evaluate_dcf_high(uint16_t duration_ms);

// called when the DCF-pin was in low-state for a given duration
void evaluate_dcf_low(uint16_t duration_ms);

// called when a pulse of the DCF-signal is evaluated as a bit
void evaluate_dcf_bit(bool bit);

// called when a pulse of the DCF-signal is evaluated as start of the DCF-signal
void evaluate_dcf_start(void);

// called when the code fails to read the DCF-signal (parity-error, corrupt data, ...)
void dcf_fail(void);



void dcf_init(void) {
  dcf_pin_state_save = 0;
  dcf_pin_ms_count = 0;
  last_dcf_pin_state = 0;
  last_dcf_pin_ms = 0;
  dcf_reset();
}



void dcf_step(void) {
  if(last_dcf_pin_ms) {
    if(last_dcf_pin_state) {
      evaluate_dcf_high(last_dcf_pin_ms);
    } else {
      evaluate_dcf_low(last_dcf_pin_ms);
    }
    // reset duration-variable -> we don't want to evaluate the same state again
    last_dcf_pin_ms = 0;
  }
}



void dcf_reset(void) {
  dcf_state = WAIT_START;
  dcf_minute = 0;
  dcf_hour = 0;
  dcf_day = 0;
  dcf_month = 0;
  dcf_year = 0;
  dcf_parity = false;
  dcf_commit_time = false;
}



void evaluate_dcf_high(uint16_t duration_ms) {
  if(duration_ms > 40 && duration_ms < 130) {
    // a high-pulse in the DCF-signal for 100ms represents a logical 0
    evaluate_dcf_bit(false);
#ifdef DCF77INFOSCREEN
    dcfinfo_zero_received(duration_ms);
#endif
  } else if(duration_ms > 140 && duration_ms < 230) {
    // a high-pulse in the DCF-signal for 200ms represents a logical 1
    evaluate_dcf_bit(true);
#ifdef DCF77INFOSCREEN
    dcfinfo_one_received(duration_ms);
#endif
  } else {
    // a pulse of any other length is an error in the DCF-signal
    dcf_fail();
#ifdef DCF77INFOSCREEN
    dcfinfo_errorbyte_received(duration_ms);
#endif
  }
}



void evaluate_dcf_low(uint16_t duration_ms) {
  if(duration_ms > 1600 && duration_ms < 2000) {
    // a low-pulse of this length means that one bit was skipped.
    // This indicates the start of the DCF signal.
    evaluate_dcf_start();
#ifdef DCF77INFOSCREEN
    dcfinfo_start_received(duration_ms);
#endif
  }

  if(dcf_commit_time) {
    // we have successfully read a new time. Send it to the RTC-chip

    // was a date received last minute?
    bool commit_date = (dcf_day || dcf_month || dcf_year);

    // the read time is valid in the next minute
    // subtract one minute
    if(dcf_minute > 0) {
      dcf_minute--;
    } else {
      dcf_minute = 59;
      if(dcf_hour > 0) {
       dcf_hour--;
      } else {
	dcf_hour = 23;
      }
    }

    if(!(dcf_minute || dcf_hour)) {
      // on midnight the last read date is invalid.
      commit_date = false;
    }

    if(commit_date) {
      writei2ctime(36, dcf_minute, dcf_hour, 0, dcf_day, dcf_month, dcf_year);
      date_y = dcf_year;
      date_m = dcf_month;
      date_d = dcf_day;
    } else {
      // use the date received from the RTC
      writei2ctime(36, dcf_minute, dcf_hour, 0, date_d, date_m, date_y);
    }
    time_h = dcf_hour;
    time_m = dcf_minute;
    time_s = 36;

    // ready to receive a new date
    dcf_year = 0;
    dcf_month = 0;
    dcf_day = 0;
    dcf_hour = 0;
    dcf_minute = 0;
    dcf_commit_time = false;

#ifdef DCF77INFOSCREEN
    dcfinfo_reset_last();
#endif
  }
}



void evaluate_dcf_bit(bool bit) {
  switch(dcf_state) {
  case WAIT_START:
    break; // continue waiting

  case CUSTOM_BIT0:
  case CUSTOM_BIT1:
  case CUSTOM_BIT2:
  case CUSTOM_BIT3:
  case CUSTOM_BIT4:
  case CUSTOM_BIT5:
  case CUSTOM_BIT6:
  case CUSTOM_BIT7:
  case CUSTOM_BIT8:
  case CUSTOM_BIT9:
  case CUSTOM_BIT10:
  case CUSTOM_BIT11:
  case CUSTOM_BIT12:
  case CUSTOM_BIT13:
  case CUSTOM_BIT14:
  case RBIT:
  case TIMESHIFT:
  case SUMMERTIME:
  case WINTERTIME:
  case LEAPSECOND:
    // all these bits can be ignored
    dcf_state++;
    break;

  case STARTBIT:
    // this bit must be 1
    if(bit) {
      dcf_state++;
    } else {
      dcf_fail();
    }
    break;

  case MINUTE_BIT0:
  case MINUTE_BIT1:
  case MINUTE_BIT2:
  case MINUTE_BIT3:
  case MINUTE_BIT4:
  case MINUTE_BIT5:
  case MINUTE_BIT6:
    // evaluate decimal-coded minute
    if(bit) {
      dcf_minute += DcfDecimalBitValues[dcf_state-MINUTE_BIT0];
    }
    dcf_parity ^= bit;
    dcf_state++;
    break;

  case MINUTE_PARITY:
    if(dcf_parity == bit) {
      dcf_parity = false;
      dcf_state++;
    } else {
      dcf_fail();
    }
    break;

  case HOUR_BIT0:
  case HOUR_BIT1:
  case HOUR_BIT2:
  case HOUR_BIT3:
  case HOUR_BIT4:
  case HOUR_BIT5:
    // evaluate decimal-coded hour
    if(bit) {
      dcf_hour += DcfDecimalBitValues[dcf_state-HOUR_BIT0];
    }
    dcf_parity ^= bit;
    dcf_state++;
    break;

  case HOUR_PARITY:
    if(dcf_parity == bit) {
      // time read successfully. Commit it on the next rising edge.
      // If the date was read in the last minute, commit it, too.
      dcf_parity = false;
      dcf_state++;
      dcf_commit_time = true;
    } else {
      dcf_fail();
    }
    break;

  case DAY_BIT0:
  case DAY_BIT1:
  case DAY_BIT2:
  case DAY_BIT3:
  case DAY_BIT4:
  case DAY_BIT5:
    // evaluate decimal-coded day
    if(bit) {
      dcf_day += DcfDecimalBitValues[dcf_state-DAY_BIT0];
    }
    dcf_parity ^= bit;
    dcf_state++;
    break;

  case DAYOFWEEK_BIT0:
  case DAYOFWEEK_BIT1:
  case DAYOFWEEK_BIT2:
    // the value of "DAYOFWEEK" is not interesting. Just calculate the parity.
    dcf_parity ^= bit;
    dcf_state++;
    break;

  case MONTH_BIT0:
  case MONTH_BIT1:
  case MONTH_BIT2:
  case MONTH_BIT3:
  case MONTH_BIT4:
    // evaluate decimal-coded month
    if(bit) {
      dcf_month += DcfDecimalBitValues[dcf_state-MONTH_BIT0];
    }
    dcf_parity ^= bit;
    dcf_state++;
    break;

  case YEAR_BIT0:
  case YEAR_BIT1:
  case YEAR_BIT2:
  case YEAR_BIT3:
  case YEAR_BIT4:
  case YEAR_BIT5:
  case YEAR_BIT6:
  case YEAR_BIT7:
    // evaluate decimal-coded year
    if(bit) {
      dcf_year += DcfDecimalBitValues[dcf_state-YEAR_BIT0];
    }
    dcf_parity ^= bit;
    dcf_state++;
    break;

  case DATE_PARITY:
    // date-parity covers the fields day, dayofweek, month and year.
    if(dcf_parity == bit) {
      // ready for next signal. The date will be committed in 37 seconds
      dcf_state = WAIT_START;
      dcf_parity = false;
    } else {
      dcf_fail();
    }
    break;

  default:
    // not a value of the enum
    dcf_fail();
    break;
  }
}



void evaluate_dcf_start(void) {
  dcf_state = WAIT_START + 1; // ready to receive first bit
#ifdef DCF77INFOSCREEN
  dcfinfo_status_read();
#endif
}



void dcf_fail(void) {
  // just wait for the next signal and hope the evaluation does not fail again
  dcf_reset();
#ifdef DCF77INFOSCREEN
  dcfinfo_status_fail();
#endif
}

#endif
