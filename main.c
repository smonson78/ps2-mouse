#include <string.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/delay_basic.h>
#include <util/delay.h>
#include <stdint.h>

// ____XXXX
// __XXXX__
uint8_t cycle[4] = {0, 1, 3, 2};

// Current Atari mouse cursor position
volatile uint16_t atari_x = 0x8000;
volatile uint16_t atari_y = 0x8000;
// Current PS/2 mouse position (the Atari mouse will try to catch up to this)
volatile uint16_t ps2_x = 0x8000;
volatile uint16_t ps2_y = 0x8000;

// For the use of the PS/2 interrupt routine
volatile uint16_t data = 0;
volatile uint8_t bits = 0;

// State of the mouse
volatile uint8_t state = 0;
volatile uint8_t device_id = 0;
volatile uint8_t mouse_enabled = 0;

//volatile uint8_t packet_received = 0;
volatile uint8_t packet[3];

// Accumulate the position of the PS2 mouse once a packet is received
void acc_ps2_position() {

  // Keep the counters near the centre range

  if (atari_x >= 0x8010) {
    ps2_x -= (atari_x - 0x8000) & 0xFFF0;
    atari_x -= (atari_x - 0x8000) & 0xFFF0;
  } else if (atari_x < 0x7FF0) {
    ps2_x += (0x8000 - atari_x) & 0xFFF0;
    atari_x += (0x8000 - atari_x) & 0xFFF0;
  }

  if (atari_y >= 0x8010) {
    ps2_y -= (atari_y - 0x8000) & 0xFFF0;
    atari_y -= (atari_y - 0x8000) & 0xFFF0;
  } else if (atari_y <= 0x7FF0) {
    ps2_y += (0x8000 - atari_y) & 0xFFF0;
    atari_y += (0x8000 - atari_y) & 0xFFF0;
  }

  int16_t movement = packet[1];
  // Extend sign
  if (packet[0] & 0x10)
    movement |= 0xFF00;

  ps2_x -= movement;

  movement = packet[2];
  // Extend sign
  if (packet[0] & 0x20)
    movement |= 0xFF00;

  ps2_y += movement;
}

#define CLOCK_HIGH (PIND & _BV(2))
#define CLOCK_LOW (!(PIND & _BV(2)))

void ps2_write(uint8_t value, uint8_t newstate) {

  uint16_t shift = value << 1;

  // calculate parity
  uint8_t parity = 0;
  for (uint8_t i = 0; i < 8; i++) {
    if (value & (1 << i))
      parity++;
  }

  // ODD parity
  if ((parity & 1) == 0)
    shift |= (1 << 9);

  // no interrupts
  cli();
  
  // LOW clock for >= 0.1mS
  PORTD &= ~_BV(2); // assert low
  DDRD |= _BV(2); // enable pin output
  _delay_loop_2(400);
  
  // LOW data (start bit)
  PORTD &= ~_BV(3); // assert low
  DDRD |= _BV(3); // enable pin output
  _delay_loop_2(100);

  // release clock
  DDRD &= ~_BV(2); // disable output
  PORTD |= _BV(2); // assert high (pullup)

  for (uint8_t i = 0; i < 10; i++) {

    // move the next bit (first is start bit - 0)
    if ((shift & 1) != 0) {
      PORTD |= _BV(3); // high
    } else {
      PORTD &= ~_BV(3); // low
    }
    shift >>= 1;
    
    // Wait for clock high
    while (CLOCK_LOW)
      ;

    // Wait for clock transition to low (when the bit is seen)
    while (CLOCK_HIGH)
      ;
  }

  // release data pin
  DDRD &= ~_BV(3); // disable pin output
  PORTD |= _BV(3); // assert high (pullup)

  // Wait for clock high
  while (CLOCK_LOW)
    ;

  // Wait for clock and data to both transition to low
  while (CLOCK_HIGH || (PIND & _BV(3)))
    ;

  // Wait for clock and data to both transition to high
  while (CLOCK_LOW || ((PIND & _BV(3)) == 0))
    ;

  // Abandon any half-read data
  bits = 0;
  data = 0;

  // So we'll know what to do with the next response from the mouse
  state = newstate;
  
  // Resume reading by interrupts
  sei();
}


ISR(INT0_vect)
{
  
  // read
  data >>= 1;
  if (PIND & _BV(3))
    data |= 0x400;
    
  bits++;

  if (bits < 11)
    return;
    
  uint8_t result = (data >> 2) & 0xFF;
  bits = 0;

  // In streaming mode - receiving packet bytes
  if (state == 100) { // (streaming) expecting packet[0]
    packet[0] = result;
    state = 101;
  } else if (state == 101) { // expecting packet[1]
    packet[1] = result;
    state = 102;
  } else if (state == 102) { // expecting packet[2]
    packet[2] = result;
    //packet_received = 1;
    state = 100;

    // Received third byte - accumulate values
    acc_ps2_position();
  } 
  
  else if (state == 1) { // Sent init command
    switch (result) {
      case 0xFA: // acknowledged
        state = 2;
        break;
      default: // error
        state = 0;
        break;
    }
  } else if (state == 2) { // Init command acknowledged
    switch (result) {
      case 0xAA: // self-test passed
        state = 3;
      break;
    }
  } else if (state == 3) {
    // Self-test passed
    device_id = result;
    if (device_id == 0)
      mouse_enabled = 1;
    state = 0;
  
  } else if (state == 10) { // expecting acknowledge FA from READ DATA (EB)
    if (result == 0xFA)
      state = 11;
    else
      state = 0;
  } else if (state == 11) { // expecting packet[0]
    packet[0] = result;
    state = 12;
  } else if (state == 12) { // expecting packet[1]
    packet[1] = result;
    state = 13;
  } else if (state == 13) { // expecting packet[2]
    packet[2] = result;
    //packet_received = 1;
    state = 0;
  } else if (state == 20) { // sent F4 enter streaming mode
    if (result == 0xFA)
    {
      state = 100; // streaming enabled
    }
    else {
      state = 0;    
    }
  } else if (state == 30) { // sent F0 enter remote mode
    
    if (result == 0xFA)
    {
      state = 50; // remote mode enabled
    }
    else
      state = 0;    
  } else if (state == 40) { // set sample rate
    if (result == 0xFA) {
      state = 41;
    }
  } else if (state == 42) { // set sample rate part 2
    if (result == 0xFA) {
      state = 0;
    }
  } else if (state == 200) { // WRAP
    if (result == 0xFA) {
      state = 201;
    }
  }

}

void setup() {

  // Start fresh.
  DDRB = 0;
  DDRD = 0;
  
  // PORTB 0,1: XA/XB 2,3: YA/YB outputs
  DDRB |= _BV(0) | _BV(1) | _BV(2) | _BV(3);

  // Mouse buttons 1 and 2
  DDRC |= _BV(0) | _BV(1);

  // LED
  DDRB |= _BV(5);

  // Pull-up on data (3) and clock (2)
  PORTD |= _BV(2) | _BV(3);


  // Set external interrupt for FALLING edge
  EICRA |= _BV(ISC01);

  // turn on INT0 external interrupt
  EIMSK |= _BV(INT0);

  sei();

  // Flash LED twice
  PORTB |= _BV(5);
  _delay_ms(250);
  PORTB &= ~_BV(5);
  _delay_ms(250);
  PORTB |= _BV(5);
  _delay_ms(250);
  PORTB &= ~_BV(5);
  
  // RESET
  ps2_write(0xFF, 1);
  
  while (mouse_enabled == 0) {
  }
  
  // Enable data reporting
  ps2_write(0xF4, 20);
  while (state < 100 || state > 102) {
  }

  // Illuminate LED to show status
  PORTB |= _BV(5);
}


void loop() {

  // Copy variables to avoid holding up the ISR
  cli();
  uint16_t copy_atari_x = atari_x;
  uint16_t copy_atari_y = atari_y;
  uint16_t copy_ps2_x = ps2_x;
  uint16_t copy_ps2_y = ps2_y;
  sei();

  // Track toward the ps2 mouse position
  if (copy_atari_x < copy_ps2_x)
    copy_atari_x++;
  else if (copy_atari_x > copy_ps2_x)
    copy_atari_x--;

  if (copy_atari_y < copy_ps2_y)
    copy_atari_y++;
  else if (copy_atari_y > copy_ps2_y)
    copy_atari_y--;

  // Update
  cli();
  atari_x = copy_atari_x;
  atari_y = copy_atari_y;
  sei();
  
  // Update mouse "encoders" using last 2 bits of position only
  uint8_t temp = PORTB;
  temp &= 0xF0;
  //temp &= 0b11101000;
  temp |= cycle[copy_atari_x & 3]; // pin 0 and 1

  // I fucked up the pin order on my PCB, we need pin 2 and 4 - FIXED
  temp |= cycle[copy_atari_y & 3] << 2;
  //temp |= (cycle[copy_atari_y & 3] & 1) << 2;
  //temp |= (cycle[copy_atari_y & 3] & 2) << 3;
  PORTB = temp;

  // Mouse buttons
  temp = PORTC;
  temp &= ~(_BV(0) | _BV(1)); // Mask out the mouse button pins
  if (!(packet[0] & 1)) // logic seems reversed?
    temp |= _BV(0);
  if (!(packet[0] & 2))
    temp |= _BV(1);
  PORTC = temp;

  _delay_us(500); // Don't go too fast for the Atari
}


int main()
{    
    setup();

    while (1) {
        loop();
    }

    return 0;
}
