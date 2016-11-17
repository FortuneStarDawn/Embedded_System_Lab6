# Embedded_System_Lab6

Practice UART asynchronous communication with timer0A

Use CCR0 to transport, and CCR1 to receive

The format is 7-E-1

First, it will flash green LED in 0.5Hz and detect temparature every second

When the temparature is high enough, "Hot!" will be printed out in terminal every second and flash red LED in 2Hz

If user input "Ack!" in terminal, it will turn back to flash green LED and detect temparature

Otherwise, it will keep printing "Hot!" and flash red LED
