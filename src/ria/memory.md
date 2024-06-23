# RIA Memory Map

Following is the memory mapping of registers of hardware interfaced by RIA.

## RIA - Raspberry Interface Adapter

| Address     | Name     | Fn   | Description                                                                                              |
| ----------- | -------- | ---- | -------------------------------------------------------------------------------------------------------- |
| $00FFC0,1   | OPERA    | MATH | Operand A for multiplication and division.                                                               |
| $00FFC2,3   | OPERB    | MATH | Operand B for multiplication and division.                                                               |
| $00FFC4,5   | MULAB    | MATH | OPERA \* OPERB.                                                                                          |
| $00FFC6,7   | DIVAB    | MATH | Signed OPERA / unsigned OPERB.                                                                           |
| $00FFC8     |          |      |                                                                                                          |
| $00FFC9     |          |      |                                                                                                          |
| $00FFCA     |          |      |                                                                                                          |
| $00FFCB     |          |      |                                                                                                          |
| $00FFCC     |          |      |                                                                                                          |
| $00FFCD     |          |      |                                                                                                          |
| $00FFCE     |          |      |                                                                                                          |
| $00FFCF     |          |      |                                                                                                          |
| $00FFD0,1,2 | ADDRSRC  | DMA  | DMA source address.                                                                                      |
| $00FFD3     | STEPSRC  | DMA  | DMA source step.                                                                                         |
| $00FFD4,5,6 | ADDRDST  | DMA  | DMA destination address.                                                                                 |
| $00FFD7     | STEPDST  | DMA  | DMA destination step.                                                                                    |
| $00FFD8     | COUNT    | DMA  | DMA transfers count.<br>write - start the DMA transfer.<br>read - non-zero when transfer is in-progress. |
| $00FFD9     | DMAERR   | DMA  | DMA transfer errno.                                                                                      |
| $00FFDA     | FDA      | FS   | File-descriptor A number. (Obtained from open() API call.)                                               |
| $00FFDB     | FDARW    | FS   | Read bytes from the FDA.<br>Write bytes to the FDA.                                                      |
| $00FFDC     | FDB      | FS   | File-descriptor B number.                                                                                |
| $00FFDD     | FDBRW    | FS   | Read bytes from the FDB.<br>Write bytes to the FDB.                                                      |
| $00FFDE     |          |      |                                                                                                          |
| $00FFDF     |          |      |                                                                                                          |
| $00FFE0     | READY    | UART | Flow control for UART FIFO.<br>bit 7 - TX FIFO not full. Ok to send.<br>bit 6 - RX FIFO has data ready.  |
| $00FFE1     | TX, RX   | UART | Write bytes to the UART.<br>Read bytes from the UART.                                                    |
| $00FFE2,3   | RNG      | HW   | Random Number Generator                                                                                  |
| $00FFE4,5   | COP      | CPU  | 65816 vector.                                                                                            |
| $00FFE6,7   | BRK      | CPU  | 65816 vector.                                                                                            |
| $00FFE8,9   | ABORTB   | CPU  | 65816 vector.                                                                                            |
| $00FFEA,B   | NMIB     | CPU  | 65816 vector.                                                                                            |
| $00FFEC     |          |      |                                                                                                          |
| $00FFED     |          |      |                                                                                                          |
| $00FFEE,F   | IRQB     | CPU  | 65816 vector.                                                                                            |
| $00FFF0     | STACK    | API  | 512 bytes for passing call parameters.                                                                   |
| $00FFF1     | OP       | API  | Write the API operation id here to begin a kernel call.                                                  |
| $00FFF2     | ERRNO    | API  | API error number.                                                                                        |
| $00FFF3     | BUSY     | API  | Bit 7 high while operation is running.                                                                   |
| $00FFF4,5   | COP      | CPU  | 65816 vector.                                                                                            |
| $00FFF6     |          |      |                                                                                                          |
| $00FFF7     |          |      |                                                                                                          |
| $00FFF8,9   | ABORTB   | CPU  | 65816 vector.                                                                                            |
| $00FFFA,B   | NMIB     | CPU  | 6502 vector.                                                                                             |
| $00FFFC,D   | RESETB   | CPU  | 6502 vector.                                                                                             |
| $00FFFE,F   | IRQB/BRK | CPU  | 6502 vector.                                                                                             |

## CGIA - Color Graphic Interface Adaptor

| Address | Name  |     | Description                                                                                                                 |
| ------- | ----- | --- | --------------------------------------------------------------------------------------------------------------------------- |
| $00FFC0 | VSYNC |     | Increments every 1/60 second when PIX VGA device is connected.                                                              |
| $00FFC1 | IRQ   |     | Set bit 0 high to enable VSYNC interrupts.<br>Verify source with VSYNC then read or write this register to clear interrupt. |

## SD-1 - Yamaha YMF825
