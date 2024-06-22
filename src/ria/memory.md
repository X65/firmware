# RIA Memory Map

Following is the memory mapping of registers of hardware interfaced by RIA.

## RIA - Raspberry Interface Adapter

| Address   | Name     |     | Description                                                                                              |
| --------- | -------- | --- | -------------------------------------------------------------------------------------------------------- |
| $00FFE0   | READY    |     | Flow control for UART FIFO.<br>bit 7 - TX FIFO not full. Ok to send.<br>bit 6 - RX FIFO has data ready.  |
| $00FFE1   | TX       |     | Write bytes to the UART.                                                                                 |
| $00FFE2   | RX       |     | Read bytes from the UART.                                                                                |
| $00FFE3   | OP       |     | Write the API operation id here to begin a kernel call.                                                  |
| $00FFE4,5 | COP      | CPU | 65816 vector.                                                                                            |
| $00FFE6,7 | BRK      | CPU | 65816 vector.                                                                                            |
| $00FFE8,9 | ABORTB   | CPU | 65816 vector.                                                                                            |
| $00FFEA,B | NMIB     | CPU | 65816 vector.                                                                                            |
| $00FFEC   | STACK    |     | 512 bytes for passing call parameters.                                                                   |
| $00FFED   | ERRNO    |     | Error number. All errors fit in this byte.                                                               |
| $00FFEE,F | IRQB     | CPU | 65816 vector.                                                                                            |
| $00FFF0   | ADDR0    |     | DMA source address.                                                                                      |
| $00FFF1   | STEP0    |     | DMA source step.                                                                                         |
| $00FFF2   | ADDR1    |     | DMA destination address.                                                                                 |
| $00FFF3   | STEP1    |     | DMA destination step.                                                                                    |
| $00FFF4,5 | COP      | CPU | 65816 vector.                                                                                            |
| $00FFF6   | DMACNT   |     | DMA transfers count.<br>write - start the DMA transfer.<br>read - non-zero when transfer is in-progress. |
| $00FFF7   | BUSY     |     | Bit 7 high while API operation is running.                                                               |
| $00FFF8,9 | ABORTB   | CPU | 65816 vector.                                                                                            |
| $00FFFA,B | NMIB     | CPU | 6502 vector.                                                                                             |
| $00FFFC,D | RESETB   | CPU | 6502 vector.                                                                                             |
| $00FFFE,F | IRQB/BRK | CPU | 6502 vector.                                                                                             |

## CGIA - Color Graphic Interface Adaptor

| Address | Name  |     | Description                                                                                                                 |
| ------- | ----- | --- | --------------------------------------------------------------------------------------------------------------------------- |
| $00FFE3 | VSYNC |     | Increments every 1/60 second when PIX VGA device is connected.                                                              |
| $00FFF0 | IRQ   |     | Set bit 0 high to enable VSYNC interrupts.<br>Verify source with VSYNC then read or write this register to clear interrupt. |

## SD-1 - Yamaha YMF825
