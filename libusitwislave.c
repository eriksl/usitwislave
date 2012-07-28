/*	See LICENSE for Copyright etc. */

#include <avr/io.h>
#include <avr/interrupt.h>

#include "usitwislave_devices.h"
#include "usitwislave.h"

#define always_inline __attribute__((always_inline))

enum
{
	of_state_check_address,
	of_state_send_data,
	of_state_request_ack,
	of_state_check_ack,
	of_state_receive_data,
	of_state_store_data_and_send_ack
} overflow_state_t;

enum
{
	ss_state_before_start,
	ss_state_after_start,
	ss_state_address_selected,
	ss_state_data_processed
} startstop_state_t;

enum
{
	buffer_size = 16
};

static void (*idle_callback)(void);
static void	(*data_callback)(uint8_t buffer_size,
						uint8_t volatile input_buffer_length, const volatile uint8_t *input_buffer,
						uint8_t volatile *output_buffer_length, uint8_t volatile *output_buffer);

static volatile	uint8_t of_state;
static volatile uint8_t ss_state;

static volatile uint8_t	slave_address;

static volatile uint8_t	input_buffer[buffer_size];
static volatile uint8_t	input_buffer_length;
static volatile uint8_t	output_buffer[buffer_size];
static volatile uint8_t	output_buffer_length;
static volatile uint8_t	output_buffer_current;

static always_inline void set_sda_to_input(void)
{
	DDR_USI &= ~_BV(PORT_USI_SDA);
}

static always_inline void set_sda_to_output(void)
{
	DDR_USI |= _BV(PORT_USI_SDA);
}

static always_inline void set_scl_to_input(void)
{
	DDR_USI &= ~_BV(PORT_USI_SCL);
}

static always_inline void set_scl_to_output(void)
{
	DDR_USI |= _BV(PORT_USI_SCL);
}

static always_inline void set_sda_low(void)
{
	PORT_USI &= ~_BV(PORT_USI_SDA);
}

static always_inline void set_sda_high(void)
{
	PORT_USI |= _BV(PORT_USI_SDA);
}

static always_inline void set_scl_low(void)
{
	PORT_USI &= ~_BV(PORT_USI_SCL);
}

static always_inline void set_scl_high(void)
{
	PORT_USI |= _BV(PORT_USI_SCL);
}

static always_inline void cr_enable_ovf_int(void)
{
	USICR |= _BV(USIOIE);
}

static always_inline void cr_enable_hold_clock_on_ovf(void)
{
	USICR |= _BV(USIWM0);
}

static always_inline void twi_reset(void)
{
	USIDR = 0;

	// make sure no sda/scl remains pulled up or down

	set_sda_to_input();		//	deactivate internal pullup on sda/scl
	set_sda_low();
	set_scl_to_input();	
	set_scl_low();

	set_sda_to_output();	//	release (set high) on sda/scl
	set_sda_high();
	set_sda_to_input();
	set_scl_to_output();
	set_scl_high();

	USICR =
		(1 << USISIE) |									// enable start condition interrupt
		(0 << USIOIE) |									// disable overflow interrupt
		(1 << USIWM1) | (0 << USIWM0) |					// set usi in two-wire mode, no bit counter overflow hold
		(1 << USICS1) | (0 << USICS0) | (0 << USICLK) |	// shift register clock source = external, positive edge, 4-bit counter source = external, both edges
		(0 << USITC);									// don't toggle clock-port pin

	USISR =
		(1		<< USISIF)	|		// clear start condition flag
		(1		<< USIOIF)	|		// clear overflow condition flag
		(0		<< USIPF)	|		// don't clear stop condition flag
		(1		<< USIDC)	|		// clear arbitration error flag
		(0x00	<< USICNT0);		// set counter to "8" bits
}

static always_inline void twi_init(void)
{
#if defined(USIPP)
#if  defined(USI_ON_PORT_A)
	USIPP |= _BV(USIPOS);
#else
	USIPP &= ~_BV(USIPOS);
# endif
#endif

	twi_reset();
}

static always_inline void initiate_ack(void)
{
	USISR =
		(0		<< USISIF)	|		// don't clear start condition flag
		(1		<< USIOIF)	|		// clear overflow condition flag
		(0		<< USIPF)	|		// don't clear stop condition flag
		(1		<< USIDC)	|		// clear arbitration error flag
		(0x0e	<< USICNT0);		// set counter to "1" bit

	USIDR = 0x00;				//	prefill bit shift register for ACK (= 0)
}

static always_inline void initiate_send_ack(void)
{
	set_sda_to_output();
	initiate_ack();
}

static always_inline void initiate_receive_ack(void)
{
	set_sda_to_input();
	initiate_ack();
}

static always_inline void initiate_data(void)
{
	USISR = 
		(0		<< USISIF)	|		//	don't clear start condition flag
		(1		<< USIOIF)	|		//	clear overflow condition flag
		(0		<< USIPF)	|		//	don't clear stop condition flag
		(1		<< USIDC)	|		//	clear arbitration error flag
		(0x00	<< USICNT0);		//	set counter to "8" bits
}

static always_inline void initiate_send_data(void)
{
	set_sda_to_output();
	initiate_data();
}

static always_inline void initiate_receive_data(void)
{
	set_sda_to_input();
	initiate_data();
}

ISR(USI_START_vect)
{
	set_sda_to_input();

	// wait for SCL to go low to ensure the start condition has completed (the
	// start detector will hold SCL low) - if a stop condition arises then leave
	// the interrupt to prevent waiting forever - don't use USISR to test for stop
	// condition as in Application Note AVR312 because the stop condition Flag is
	// going to be set from the last TWI sequence
	
	while(!(PIN_USI & _BV(PIN_USI_SDA)) &&
			(PIN_USI & _BV(PIN_USI_SCL)))

	// possible combinations
	//	sda = low	scl = low		break	start condition
	// 	sda = low	scl = high		loop
	//	sda = high	scl = low		break	stop condition
	//	sda = high	scl = high		break	stop condition
	
	if(!(PIN_USI & _BV(PIN_USI_SDA)))	// start condition
	{
		of_state = of_state_check_address;
		ss_state = ss_state_after_start;

		USIDR = 0;

		USISR =
			(1		<< USISIF)	|		// clear start condition flag
			(0		<< USIOIF)	|		// don't clear overflow condition flag
			(0		<< USIPF)	|		// don't clear stop condition flag
			(0		<< USIDC)	|		// don't clear arbitration error flag
			(0x00	<< USICNT0);		// set counter to "8" bits

		cr_enable_hold_clock_on_ovf();
		cr_enable_ovf_int();
	}
	else // stop condition
		twi_reset();
}

ISR(USI_OVERFLOW_VECTOR)
{
	// bit shift register overflow condition occured
	// scl forced low until overflow condition is cleared!
	
	uint8_t repeat;

	do
	{
		repeat = 0;	// do not repeat eternally

		switch(of_state)
		{
			// start condition occured and succeed
			// check address, if not OK, reset usi
			// note: not using general call address

			case(of_state_check_address):
			{
				uint8_t data;
				uint8_t address;
				uint8_t direction;

				data		= USIDR;
				direction	= data & 0x01;
				address		= (data & 0xfe) >> 1;

				if(address == slave_address)
				{
					ss_state = ss_state_address_selected;

					if(direction)					// read request from master
						of_state = of_state_send_data;
					else							// write request from master
						of_state = of_state_receive_data;

					initiate_send_ack();
				}
				else
				{
					ss_state = ss_state_before_start;
					twi_reset();
				}

				break;
			}

			// process read request from master

			case(of_state_send_data):
			{
				ss_state = ss_state_data_processed;

				if(output_buffer_current < output_buffer_length)
					USIDR = output_buffer[output_buffer_current++];
				else
					USIDR = 0xff;

				initiate_send_data();
				of_state = of_state_request_ack;

				break;
			}

			// data sent to master, request ack (or nack) from master

			case(of_state_request_ack):
			{
				of_state = of_state_check_ack;
				initiate_receive_ack();
				break;
			}

			// ack/nack from master received
			
			case(of_state_check_ack):
			{
				if(USIDR)	// if NACK, the master does not want more data
				{
					of_state = of_state_check_address;
					twi_reset();
				}
				else
				{
					of_state = of_state_send_data;
					repeat = 1;	// from here we just drop straight into state_send_data
				}				// don't wait for another overflow interrupt

				break;
			}

			// process write request from master

			case(of_state_receive_data):
			{
				ss_state = ss_state_data_processed;

				of_state = of_state_store_data_and_send_ack;
				initiate_receive_data();
				break;
			}

			// data received from master, store it and wait for more data

			case(of_state_store_data_and_send_ack):
			{
				if(input_buffer_length < (buffer_size - 1))
					input_buffer[input_buffer_length++] = USIDR;

				of_state = of_state_receive_data;
				initiate_send_ack();
				break;
			}
		}
	}
	while(repeat); // allow for some overflow states to go into another state immediately, without waiting for overflow condition
}

void usi_twi_slave(uint8_t slave_address_in,
			void (*data_callback_in)(uint8_t buffer_size,
			volatile uint8_t input_buffer_length, volatile const uint8_t *input_buffer,
			volatile uint8_t *output_buffer_length, volatile uint8_t *output_buffer),
			void (*idle_callback_in)(void))
{
	slave_address			= slave_address_in;
	data_callback			= data_callback_in;
	idle_callback			= idle_callback_in;

	input_buffer_length		= 0;
	output_buffer_length	= 0;
	output_buffer_current	= 0;
	ss_state				= ss_state_before_start;

	twi_init();

	sei();

	for(;;)
	{
		if(USISR & _BV(USIPF))
		{
			cli();

			USISR |= _BV(USIPF);	// clear stop condition flag

			switch(ss_state)
			{
				case(ss_state_before_start):	//	gets removed while compilation
				{
					break;
				}

				case(ss_state_after_start):
				{
					twi_reset();
					break;
				}

				case(ss_state_address_selected)://	gets removed during compilation
				{
					break;
				}

				case(ss_state_data_processed):
				{

					output_buffer_length	= 0;
					output_buffer_current	= 0;

					data_callback(buffer_size, input_buffer_length, input_buffer, &output_buffer_length, output_buffer);

					input_buffer_length		= 0;

					break;
				}
			}

			ss_state = ss_state_before_start;

			sei();
		}

		if(idle_callback)
			idle_callback();
	}
}
