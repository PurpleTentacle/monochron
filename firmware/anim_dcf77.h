#ifndef ANIM_DCF77_H_
#define ANIM_DCF77_H_

// initializes the info screen
void initanim_dcfinfo(void);

// called every 75ms to update the info
void step_dcfinfo(void);

// called when starting to evaluate a new DCF-signal
void dcfinfo_status_read(void);

// called when an error occurred during evaluation of the DCF-signal
void dcfinfo_status_fail(void);

// called when a new DCF-signal was read successfully to reset the last-valid-signal-counter
void dcfinfo_reset_last(void);

// informs the infoscreen about the duration of the last received logical 0
void dcfinfo_zero_received(uint16_t duration_ms);

// informs the infoscreen about the duration of the last received logical 1
void dcfinfo_one_received(uint16_t duration_ms);

// informs the infoscreen about the duration of the last received start sequence
void dcfinfo_start_received(uint16_t duration_ms);

// informs the infoscreen about the duration of the last pulse, that could not be interpreted
void dcfinfo_errorbyte_received(uint16_t duration_ms);

#endif
