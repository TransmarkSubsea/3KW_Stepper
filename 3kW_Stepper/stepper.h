

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