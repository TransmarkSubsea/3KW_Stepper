#include <stdint.h>
#include <stdbool.h>

// UART defines
// By default the stdout UART is `uart0`, so we will use the second one
#define UART_ID uart1
#define BAUD_RATE 115200

// Use pins 4 and 5 for UART1
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
#define UART_TX_PIN 4
#define UART_RX_PIN 5

//define stepper pins
#define Stepper_EN 16
#define Stepper_DIR 17
#define Stepper_PULSE 18
#define Near_Stop 19
#define Far_Stop 20
#define steps_per_rev 1600 //200 steps/rev with 8x microstepping
#define Direction_Open 0
#define Direction_Close !Direction_Open


// Function prototypes
void stepper_move(uint16_t speed, float travel_distance, bool direction);
void stepper_calibrate(void);
static void stepper_broadcast(const char *fmt, ...);
void limit_switch_isr(uint gpio, uint32_t events);
bool status_timer_callback(struct repeating_timer *t);
void pico_set_led(bool led_on);
int pico_led_init(void);

// Extern declarations for global variables
extern volatile bool stop_pulse;
extern volatile uint32_t last_interrupt_time;
extern const uint32_t DEBOUNCE_MS;
extern const float Step_resolution;
extern volatile uint32_t Step_actual;
extern volatile int32_t Accumulated_Steps;
extern volatile float Accumulated_Distance;
extern volatile bool Data_Rx;
extern volatile uint32_t Calibration_Steps;
extern volatile float Calibration_Distance;