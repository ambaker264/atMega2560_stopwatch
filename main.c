#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "main.h"
#include "lcd_drv.h"

//button benchmarks for easier tuning
#define DOWN_BENCHMARK 450
#define UP_BENCHMARK 250
#define LEFT_BENCHMARK 650
#define RIGHT_BENCHMARK 50
#define TIMER_TICK 0x10 //how many cycles until we want to tick up a ms of timer

void init_function();
void write_to_screen(uint8_t *, char *);
void update_times(volatile uint8_t *);
void inc_current_time(uint8_t * current_time);


//handy global variables for polling and whatnot
static volatile uint8_t button_value = 0; //no button pushed
static volatile int ping_cnt = 0; //init timer to 0
static volatile uint8_t timer_ping = 0;
static volatile uint8_t behavior[] = {0,0}; //if idx 0 is nonzero, freeze top. if idx 1 is nonzero, freeze bottom
	
// These are included by the LCD driver code, so 
// we don't need to include them here.
// #include <avr/io.h>
// #include <util/delay.h>

//timer 0 interrupt handler on compare match. this timer is to ping to display again.
ISR (TIMER0_COMPA_vect){ //isr is apparently a macro, so hopefully it will compile properly with the gnu gcc
	//this pings once every millisecond.
	ping_cnt++;	//only place this global is incremented
	timer_ping = 1;
	//have to set timer back to 0 now.
	TCNT0 = 0x00;
}

//use this interrupt when ADC Converion Complete. Writes to the behavior globals
//depending on what button was pressed.
ISR (ADC_vect){
	//get the actual value out of the adc registers, much easier to compare
	uint16_t adc_val = ADCL; //hopefully won't have type issues here, should cast
	adc_val += ADCH * 2^8; //just moving it over a bit, this should handle the issues.

	if(adc_val < RIGHT_BENCHMARK){
		//right button, do nothing		
	}else if(adc_val < UP_BENCHMARK){
		//up button, so we freeze the top line.
		behavior[0] = 1;
	}else if(adc_val < DOWN_BENCHMARK){
		//down button, unfreeze both
		behavior[0] = 0;
		behavior[1] = 0;
	}else if(adc_val < LEFT_BENCHMARK){
		//left button, freeze bottom line
		behavior[1] = 1;
	}
	//any other value is too high to do anything, so we just start another conversion
	ADCSRA |= (1<<ADSC);
}

int main(void){

	//declare all variables at start for C
	uint8_t current_time[] = {0,0,0,0,0,0,0,0,0}; //hr:min:sec:subsec
	char * out_string;
	uint8_t * old_button_val;
	uint8_t * old_time_val;

	out_string = calloc(13, sizeof(char)); //12 long, +1 for end string null char.
	out_string[2] = ':';
	out_string[5] = ':'; //c casts this for us.
	out_string[8] = '.';
	out_string[12] = 0; //end string

	old_button_val = calloc(1, sizeof(uint8_t)); //literally just 1 byte lol
	old_time_val = calloc(9, sizeof(uint8_t)); //need to init to 0, so calloc

	init_function();

	//to start, we write the initial string to screen on both levels.
	lcd_xy(0,0);
	lcd_puts(out_string);

	lcd_xy(1,0);
	lcd_puts(out_string);
	
	//polling loop, infinite we should never leave lol
	//can easily test without timers by setting timer ping to 1
	while(1){
		if(timer_ping){
			//update every millisecond. if this doesn't work, can just modulo the ping_cnt for writing
			inc_current_time(current_time);
			write_to_screen(current_time, out_string);
			timer_ping = 0;
		}
	}

	free(out_string);
	free(old_button_val);
	free(old_time_val);

	return 1;
}


/*Takes the listed parameters, the global behavior value (set by buttons),
 and puts them on the screen according with the instruction*/
void write_to_screen(uint8_t * ctime, char * o_str){
	char check_char = 0;
	uint8_t time_idx = 0;
	for(uint8_t cursor_pos = 0; cursor_pos < 12; cursor_pos++){
		check_char = atoi(&ctime[time_idx]);
		if(cursor_pos == 2 || cursor_pos == 5 || cursor_pos == 8){
			continue;
		}
		time_idx++; //increment, value now stored
		if(check_char == o_str[cursor_pos]){	
			continue;
		}
		//now we know we need to replace a char, both in o_str and onscreen
		if(behavior[0] != 0){
			lcd_xy(0, check_char);
			lcd_blank(1);
			lcd_putchar(check_char);
			o_str[cursor_pos] = check_char;
		}
		if(behavior[1] != 0){
			lcd_xy(1, check_char);
			lcd_blank(1);
			lcd_putchar(check_char);
			o_str[cursor_pos] = check_char; //this happens twice, its fine
		}	
	}	
	return;
}



void inc_current_time(uint8_t * current_time){
	/*handy trick: to fix the issue with 1024 not being a perfect match for 1ms,
	there are 24 more ms than needed. since 1000/24 is ~42, we can skip every 42nd
	ping

	we can have this skip 1 (kinda like a leap year correction) every 42 pings.
	I'm not going any more detailed than this, sorry.*/

	uint8_t to_check;

	if(!(ping_cnt % 42)){
		//if here, we skip, don't increment the counter
		return;
	}

	//now we just bump up the timer by one. going to be long since a lot of digits.
	//seconds are base 10, so we can do all of them together, along with the first of the minutes and hrs
	for(uint8_t i = 8; i>=0; i--){
		to_check = current_time[i];
		if(to_check >= 9 || ((i == 2 || i == 4) && to_check >=5)){	
			//handles all the bases nicely. if b10, then check for overflow if 9, if b6 check for 5
			current_time[i] = 0;
			continue;	
		}else{
			current_time[i] = ++to_check;
			return;
		}
	}	
	//if here we hit overflow. set all to 0.
	for(uint8_t j = 0; j<9; j++){
		current_time[j] = 0;
	}
	return;
}

void init_function(){
	//start with the lcd
	lcd_init();

	//next we initialize the buttons

	ADMUX = (1<<REFS0); //setting the aref pin with capicitor for adc
	//status register: turn on, enable interrupt, /128 prescaler.
	ADCSRA = (1<<ADEN) | (1<<ADSC) | (1<<ADIE) | (1<<ADPS2) | (1<<ADPS1) | (1<<ADPS0);


	//start the timer that we will use
	TCNT0 = 0x00;

	//now we config the control registers for each timer.
	TCCR0A = (1<<COM0A0); //enable compare match A for timer 0
	TCCR0B = (1<<CS02) | (1<<CS00); //set prescaler to 1024

	/*our timer 0 output compare register. 
	
	The math for what I will set:

	CLOCK SPEED = 1 tick per 1/2^24th of a second.

	PRESCALER SET TO 2^10.

	SO: TIMER TICKS EVERY 2^14 of a second. 
	
	TO UPDATE EVERY MILLISECOND: need to update/interrupt at every 1000th of a second.

	so 2^-14 * x = 1/1000, we solve as x is a little over 16 (2^4). SO, 
	
	*/
	OCR0A = TIMER_TICK; //2^4 is gonna be fine to update every millisecond. best we can do without floats. Subject to change.

	//now we enable the compare/match interrupt on TIMSK0
	TIMSK0 = (1<<OCIE0A);

	sei(); //global enable interrupts

}




