#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "hardware/uart.h"
#include "stepper.h"





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
    gpio_init(Stepper_PULSE);
    gpio_set_dir(Stepper_PULSE, GPIO_OUT);

    gpio_put(Stepper_EN, 0); //Enable stepper driver
    gpio_put(Stepper_DIR, 0); //Set direction to forward
    gpio_put(Stepper_PULSE, 0); //Set pulse pin low

  while (true) 
  {
    printf("Starting stepper pulse sequence: 1000 toggles with 50ms delay\n");

    for(int i = 0; i < (3200*4); i++) 
    {
        gpio_put(Stepper_PULSE, !gpio_get(Stepper_PULSE)); // Toggle the pulse pin
        sleep_us(35); // 50ms delay between transitions
    }

    printf("Stepper pulse sequence complete\n");
    gpio_put(Stepper_DIR, !gpio_get(Stepper_DIR));

    sleep_ms(2000);
    }
}
