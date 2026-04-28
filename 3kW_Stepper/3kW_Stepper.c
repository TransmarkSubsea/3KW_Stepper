#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "hardware/uart.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "stepper.h"

// Volatile flag to signal pulse generation to stop
volatile bool stop_pulse = false;
volatile uint32_t last_interrupt_time = 0;
const uint32_t DEBOUNCE_MS = 20; // Debounce time in milliseconds

// Interrupt handler for limit switches with debouncing
void limit_switch_isr(uint gpio, uint32_t events) {
    uint32_t current_time = time_us_32() / 1000; // Convert to ms
    
    // Debounce check
    if ((current_time - last_interrupt_time) < DEBOUNCE_MS) {
        return;
    }
    
    last_interrupt_time = current_time;
    
    if ((gpio == Near_Stop || gpio == Far_Stop) && (events & GPIO_IRQ_LEVEL_HIGH)) {
        if (gpio_get(Near_Stop) || gpio_get(Far_Stop)) {
            stop_pulse = true;
            printf("Limit switch activated: %s\n", gpio == Near_Stop ? "Near_Stop" : "Far_Stop");
        }
    }
}

int main()
{
    stdio_init_all();

    // Watchdog example code
    if (watchdog_caused_reboot()) {
        printf("Rebooted by Watchdog!\n");
        // Whatever action you may take if a watchdog caused a reboot
    }
    
    // Enable the watchdog, requiring the watchdog to be updated every 100ms or the chip will reboot
    // second arg is pause on debug which means the watchdog will pause when stepping through code
    //watchdog_enable(100, 1);
    
    // You need to call this function at least more often than the 100ms in the enable call to prevent a reboot
    //watchdog_update();

    printf("System Clock Frequency is %d Hz\n", clock_get_hz(clk_sys));
    printf("USB Clock Frequency is %d Hz\n", clock_get_hz(clk_usb));
    // For more examples of clocks use see https://github.com/raspberrypi/pico-examples/tree/master/clocks

    // Set up our UART
    uart_init(UART_ID, BAUD_RATE);
    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    
    // Use some the various UART functions to send out data
    // In a default system, printf will also output via the default UART
    
    // Send out a string, with CR/LF conversions
    uart_puts(UART_ID, " Hello, UART!\n");
    
    // For more examples of UART use see https://github.com/raspberrypi/pico-examples/tree/master/uart

    //Set-up stepper pins
    gpio_init(Stepper_EN);
    gpio_set_dir(Stepper_EN, GPIO_OUT);
    gpio_init(Stepper_DIR);
    gpio_set_dir(Stepper_DIR, GPIO_OUT);
    gpio_set_function(Stepper_PULSE, GPIO_FUNC_PWM); // Use PWM for step pulse output
    gpio_init(Near_Stop);
    gpio_set_dir(Near_Stop, GPIO_IN);
    gpio_init(Far_Stop);
    gpio_set_dir(Far_Stop, GPIO_IN);

    // Set up GPIO interrupts for limit switches
    gpio_set_irq_enabled(Near_Stop, GPIO_IRQ_LEVEL_HIGH, true);
    gpio_set_irq_enabled(Far_Stop, GPIO_IRQ_LEVEL_HIGH, true);
    gpio_set_irq_callback(limit_switch_isr);
    irq_set_enabled(IO_IRQ_BANK0, true);

    gpio_put(Stepper_EN, 0); //Enable stepper driver
    gpio_put(Stepper_DIR, 0); //Set direction to forward
    
    
    uint16_t speed = 1; // Set speed here, range is 1-14 where 1 is slowest and 14 is fastest
    uint32_t step_count = steps_per_rev*5; // Configure number of PWM pulses here
    uint32_t Step_actual = 0; // Actual number of pulses generated
    float pulse_frequency = 0;


    if (speed > 11)
    {
        speed = 11; // Limit speed to max of 11 to prevent exceeding max pulse frequency
    }
    else if (speed < 1)
    {
        speed = 1; // Limit speed to min of 1 to prevent zero frequency
    }
    pulse_frequency = (speed+1) *1000;   // Fixed PWM frequency in Hz, max is 14000

    while (true) 
    {
      printf("Starting stepper pulse sequence using PWM (%u pulses at %.1f Hz)\n", step_count, pulse_frequency);

      uint slice_num = pwm_gpio_to_slice_num(Stepper_PULSE);
      pwm_set_clkdiv(slice_num, 1.0f);

      uint32_t wrap = (uint32_t)((float)clock_get_hz(clk_sys) / pulse_frequency - 1.0f + 0.5f);
      if (wrap > 65535u) wrap = 65535u;
      if (wrap < 1u) wrap = 1u;
      pwm_set_wrap(slice_num, (uint16_t)wrap);
      pwm_set_gpio_level(Stepper_PULSE, (uint16_t)(wrap / 2));

      stop_pulse = false;
      Step_actual = 0;
      for (uint32_t step = 0; step < step_count; ++step) {
          if (stop_pulse) {
              pwm_set_enabled(slice_num, false);
              printf("Pulse generation stopped by limit switch\n");
              break;
          }

          pwm_set_counter(slice_num, 0);
          pwm_set_enabled(slice_num, true);

          uint32_t period_us = (uint32_t)((1.0f / pulse_frequency) * 1000000.0f + 0.5f);
          sleep_us(period_us);

          pwm_set_enabled(slice_num, false);
          Step_actual++;
      }

      printf("Stepper pulse sequence complete (actual pulses: %u)\n", Step_actual);

      gpio_put(Stepper_DIR, !gpio_get(Stepper_DIR));

      sleep_ms(2000);
    }
}
