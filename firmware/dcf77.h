#ifndef DCF77_H_
#define DCF77_H_

#define DCF_PIN PINC
#define DCF_BIT 1

// initializes the DCF-variables
// should be called once
void dcf_init(void);

// one step in evaluation of DCF time
// should be called several times in a second
void dcf_step(void);

#endif
