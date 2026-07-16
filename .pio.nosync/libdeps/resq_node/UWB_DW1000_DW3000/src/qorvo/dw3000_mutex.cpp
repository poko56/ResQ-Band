#include "dw3000_device_api.h"
#include "dw3000_port.h"

decaIrqStatus_t decamutexon(void)           
{
    noInterrupts();
    return 0;
}

void decamutexoff(decaIrqStatus_t s)
{
    (void)s;
    interrupts();
}
