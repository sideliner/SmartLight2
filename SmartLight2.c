#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>

#define F_CPU 4800000				// 4.8 MHz, built-in resonator
#define LED PB4						// pin 3
#define BUTTON PB1					// pin 6
#define BUTTON_CLOSED !(PINB & (1<<BUTTON))
#define TWO_MS_COUNT 152
#define LUX 123						// threshold for illuminance
#define CHECK_INTERVAL_MINUTES 2	// how often to check illuminance
#define SENSOR_COUNTER_MAX 8		// how many readings before toggling led
#define MIN_ON_PERIOD_MINUTES 60	// min how many minutes hold led ON

volatile uint8_t buttonStateCounter = 0;	// integral value for the button state
volatile uint32_t two_msec = 0;		// 2-millisecond counter (not precise)

#include <util/delay.h>
#include <util/atomic.h>

uint8_t adc_read (void)
{
	ADCSRA |= (1 << ADSC);			// Start the conversion
	while (ADCSRA & (1 << ADSC));	// Wait for it to finish
	return ADCH;
}

uint32_t mins()
{
	uint32_t mins_return;
	uint32_t tmp;

	// Ensure this cannot be disrupted
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		tmp = two_msec;
	}
	mins_return = (uint32_t)tmp/500/60;
	return mins_return;
}

int main(void)
{
	DDRB |= (1 << LED);				// Set LED as output

	DDRB &= ~(1 << BUTTON);			// Set BUTTON as input
	PORTB |= (1 << BUTTON);			// Enable pull-up on the button

	TCCR0A |= (1 << WGM01 );		// Configure timer for CTC
	TIMSK0 |= (1 << OCIE0B ) ;		// Enable CTC interrupt
	sei() ;							// Enable global interrupts
	OCR0A = TWO_MS_COUNT;
	TCCR0B |= (1 << CS01 );			// Start timer with prescaler 1/8

	ADMUX |= (1 << MUX0);			// Set the ADC input to PB2/ADC1
	ADMUX |= (1 << ADLAR);			// left adjust ADC result

	ADCSRA |= (1 << ADPS2) | (1 << ADPS0) | (1 << ADEN);	// Set the prescaler to clock/32 & enable ADC
	uint8_t adc_in;
	uint32_t currMinutes = 0;
	uint32_t prevMinutes = 0;
	uint8_t sensorStateCounter = 0;	// integral value for sensor state
	struct ledStatus
	{
		uint8_t state;
		uint32_t stateLastUpdated;
	} currLed;
	currLed.state = 0;
	currLed.stateLastUpdated = 0;

	for(;;)
	{
		while (buttonStateCounter >= 0xFF)
		{
			adc_in = adc_read();
			if (adc_in < LUX)
			{
				PORTB |=(1<<LED);
			}
			_delay_ms(100);
			currLed.state = 0;
			currLed.stateLastUpdated = 0;
			PORTB &= ~(1<<LED);
		}

		currMinutes = mins();
		if (currMinutes - prevMinutes >= CHECK_INTERVAL_MINUTES)
		{
			adc_in = adc_read();

			if (currLed.state == 0)				//if LED is currently OFF
			{
				if (adc_in < LUX)
				{
					if (sensorStateCounter < SENSOR_COUNTER_MAX)
					{
						sensorStateCounter++;
					}
					if (sensorStateCounter >= SENSOR_COUNTER_MAX)
					{
						PORTB |=(1<<LED);
						sensorStateCounter = 0;
						currLed.state = 1;
						currLed.stateLastUpdated = currMinutes;
					}
				}
				else
				{
					if (sensorStateCounter > 0)
					{
						sensorStateCounter--;
					}
				}
			}
			else				//LED is currently ON
			{
				if (adc_in >= LUX)
				{
					if (sensorStateCounter < SENSOR_COUNTER_MAX)
					{
						sensorStateCounter++;
					}
					if ((sensorStateCounter >= SENSOR_COUNTER_MAX) &&
					(currMinutes - currLed.stateLastUpdated > MIN_ON_PERIOD_MINUTES))
					{
						PORTB &= ~(1<<LED);
						sensorStateCounter = 0;
						currLed.state = 1;
						currLed.stateLastUpdated = currMinutes;
					}
				}
				else
				{
					if (sensorStateCounter > 0)
					{
						sensorStateCounter--;
					}
				}
			}
		}
		prevMinutes = currMinutes;

		_delay_ms(100);
	}
}

ISR(TIM0_COMPB_vect)
{
	two_msec++;
	buttonStateCounter = (buttonStateCounter << 1) | (BUTTON_CLOSED & 0b00000001);	// Shift in button reading
	// 0xFF - closed and debounced, 0x00 - open and debounced
	// any other value - transitional state
}
