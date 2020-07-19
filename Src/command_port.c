#include "stdint.h"
#include "tx_api.h"
#include "command/command.h"

void *Command_Malloc(Command_Controller *controller, uint32_t size)
{
    void *ptr = NULL;
    tx_byte_allocate(controller->outerState, &ptr, size, TX_WAIT_FOREVER);
    return ptr;
}
void Command_Mrelease(Command_Controller *controller, void *ptr)
{
    tx_byte_release(ptr);
}