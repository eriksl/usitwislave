/*	See LICENSE for Copyright etc. */

#ifndef	_USI_TWI_SLAVE_H_
#define	_USI_TWI_SLAVE_H_

void	usi_twi_slave(uint8_t slave_address,
			void (*data_callback)(uint8_t buffer_size,
			volatile uint8_t input_buffer_length, volatile const uint8_t *input_buffer,
			volatile uint8_t *output_buffer_length, volatile uint8_t *output_buffer),
			void (*idle_callback)(void));

#endif
