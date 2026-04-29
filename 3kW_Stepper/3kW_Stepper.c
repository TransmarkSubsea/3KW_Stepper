#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "hardware/uart.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "stepper.h"
#include "cli.h"



//wheel size is 18 teeth and each tooth has a pitch of 2.032 mm, so one revolution moves the wheel 36.576 mm. With 1600 steps per revolution at 8x microstepping, each step moves the wheel 0.02286 mm.
const float Step_resolution = 0.02286; //1 step moves 0.02286 mm at 8x microstepping 
const uint32_t DEBOUNCE_MS = 10; // Debounce time in milliseconds
volatile bool stop_pulse = false;// Volatile flag to signal pulse generation to stop
volatile uint32_t last_interrupt_time = 0;
volatile uint32_t Step_actual = 0; // Actual number of pulses generated
volatile int32_t Accumulated_Steps = 0; // Accumulated steps
volatile float Accumulated_Distance = 0; // Accumulated distance in mm
volatile bool Data_Rx = false; // Flag to indicate data received for CLI processing

// Function prototypes
void stepper_move(uint16_t speed, float travel_distance, bool direction);
void limit_switch_isr(uint gpio, uint32_t events);
bool status_timer_callback(struct repeating_timer *t);
void pico_set_led(bool led_on);
int pico_led_init(void);

int main()
{
    stdio_init_all();
    pico_led_init();
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
    
    // Initialize CLI system (sets up UART interrupt handler)
    cli_init();

    // Set up hardware timer for status output every second
    struct repeating_timer status_timer;
    add_repeating_timer_ms(1000, status_timer_callback, NULL, &status_timer);
    printf("> ");

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
    
    // Main loop: CLI command processing
    while (true) 
    {
        cli_process();
        // Status output is handled by hardware timer
        sleep_ms(1);
    }

}

// Hardware timer callback for status output every second
bool status_timer_callback(struct repeating_timer *t) {
    printf("STATUS: Position=%.3f mm, Steps=%ld, Moving=%s\n",
           Accumulated_Distance,
           Accumulated_Steps,
           cli_is_moving() ? "YES" : "NO");
    return true; // Keep the timer repeating
}
