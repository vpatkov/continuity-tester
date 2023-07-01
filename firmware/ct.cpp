/* Main program */

#include <stdint.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include "common.hpp"
#include "delay.hpp"
#include "gpio.hpp"
#include "tones.hpp"
#include "volatile.hpp"

/* Peripherals are configured for 1 MHz system clock */
static_assert(F_CPU == 1e6, "");

constexpr Gpio::Pin beeper = Gpio::B2;
constexpr Gpio::Pin vcc_fuel = Gpio::C0;
constexpr Gpio::Pin vcc_probe = Gpio::C3;
constexpr Gpio::Pin button = Gpio::D2;

constexpr Gpio::Pin unused_pins[] = {
	Gpio::B0,
	Gpio::B1,
	Gpio::B3,
	Gpio::B4,
	Gpio::B5,
	Gpio::B6,
	Gpio::B7,
	Gpio::C4,
	Gpio::C5,
	Gpio::D0,
	Gpio::D1,
	Gpio::D3,
	Gpio::D4,
	Gpio::D5,
	Gpio::D6,
	Gpio::D7,
};

constexpr uint8_t fuel_adc_channel = 1;
constexpr uint8_t probe_adc_channel = 2;
constexpr uint8_t iref_adc_channel = 14;

static void beep(uint16_t tone)
{
	if (tone) {
		/* Set T/C1 for 50% PWM on OC1B pin */
		TCCR1A = 1<<COM1B1| 1<<WGM11 | 1<<WGM10;
		TCCR1B = 1<<WGM13 | 1<<WGM12 | 1<<CS10;
		OCR1A = tone;
		OCR1B = tone/2;
	} else {
		/* Stop T/C1 and disconnect OC1B */
		TCCR1A = 0;
		TCCR1B = 0;
	}
}

static void play(const uint16_t *tones)
{
	uint16_t tone;
	while ((tone = pgm_read_word(tones++)) != 0) {
		beep(tone);
		delay_ms(50);
		beep(0);
		delay_ms(50);
	}
}

static uint8_t adc_measure(uint8_t channel)
{
	constexpr uint8_t nr_samples = 64;
	uint16_t acc = 0;

	for (uint8_t i = 0; i < nr_samples; i++) {
		ADMUX = channel & 15;
		ADCSRA = 1<<ADEN | 1<<ADSC | 1<<ADPS1 | 1<<ADPS0;  /* 125 kHz */
		while (ADCSRA & (1<<ADSC))
			memory_barrier();
		acc += ADC;
	}

	return acc/nr_samples/4;
}

static bool self_test()
{
	/* Check internal (1.1 V) vs external (1.24 V) references */
	constexpr uint8_t iref_min = 1.1 * 256/1.24 * 0.9;
	constexpr uint8_t iref_max = 1.1 * 256/1.24 * 1.1;
	uint8_t iref = adc_measure(iref_adc_channel);
	if (iref < iref_min || iref > iref_max)
		return false;

	/* Check the battery voltage */
	constexpr uint8_t fuel_min = 2.7/4 * 256/1.24;
	uint8_t fuel = adc_measure(fuel_adc_channel);
	if (fuel < fuel_min)
		return false;

	return true;
}

static bool power_up()
{
	Gpio::write(vcc_probe, 1);
	Gpio::write(vcc_fuel, 1);
	delay_ms(100);
	bool r = self_test();
	Gpio::write(vcc_fuel, 0);
	while (Gpio::read(button) == 0)
		delay_ms(100);
	return r;
}

EMPTY_INTERRUPT(INT0_vect);

static void power_down()
{
	Gpio::write(vcc_probe, 0);
	while (Gpio::read(button) == 0)
		delay_ms(100);
	EIMSK = 1<<INT0;
	ADCSRA = 0;  /* Disable ADC */
	set_sleep_mode(SLEEP_MODE_PWR_DOWN);
	sleep_mode();
	EIMSK = 0;
}

EMPTY_INTERRUPT(ADC_vect);

static void work()
{
	/* ADC: free running, ~4.8 kHz sample rate (1M/16/13) */
	ADMUX = 1<<ADLAR | (probe_adc_channel & 15);
	ADCSRA = 1<<ADEN | 1<<ADSC | 1<<ADATE | 1<<ADIF | 1<<ADIE | 1<<ADPS2;
	ADCSRB = 0;

	uint16_t inactivity_timer = 0;
	set_sleep_mode(SLEEP_MODE_IDLE);

	for (uint8_t cycle = 0;; cycle++) {
		sleep_mode();

		/* ~38 Hz */
		if (cycle % 128 == 0) {
			if (Gpio::read(button) == 0)
				break;
			if (++inactivity_timer > 180.0 * 1e6/16/13/128)  /* 3 minutes */
				break;
		}

		static uint16_t prev_tone = -1;
		uint16_t tone = pgm_read_word(r_tones + ADCH);
		if (tone < prev_tone || (tone != 0 && prev_tone == 0)) {  /* Auto hold and reset */
			if (tone == 0)
				delay_ms(100);  /* Ensured beep duration */
			beep(tone);
			// if (tone == 0)
			// 	delay_ms(100);  /* Ensured silence duration */
			prev_tone = tone;
			inactivity_timer = 0;
		}
		
	}
}

int main()
{
	/* GPIO init */
	Gpio::write(beeper, 0);
	for (auto p : unused_pins)
		Gpio::pull_up(p);
	
	/* Disable unused peripherals */
	ACSR = 1<<ACD;
	DIDR0 = 0b111111;
	DIDR1 = 0b11;
	PRR = 1<<PRTWI | 1<<PRTIM2 | 1<<PRTIM0 | 1<<PRSPI | 1<<PRUSART0;

	sei();
	
	for (;;) {
		if (power_up()) {
			play(power_up_sound);
			work();
			play(power_down_sound);
			power_down();
		} else {
			play(error_sound);
			power_down();
		}
	}

	return 0;
}
