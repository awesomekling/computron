/* vomit.c
 * Main initialization procedures
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "vomit.h"
#include "debug.h"
#include "iodevice.h"
#include <QDebug>

vomit_options_t options;

static void vm_init();

int vomit_init(int argc, char **argv)
{
    vm_init();
    vm_loadconf();
    return 0;
}

void vm_init() {
    vlog( VM_INITMSG, "Initializing CPU" );
    vomit_cpu_init(&g_cpu);
    vlog( VM_INITMSG, "Initializing video BIOS" );
    video_bios_init();

    for (dword i = 0; i <= 0xFFFF; ++i)
        vm_listen(i, 0L, 0L);

    for (BYTE i = 0xE0; i <= 0xEF; ++i)
        vm_listen(i, 0L, vm_call8);

    vlog( VM_INITMSG, "Registering I/O devices" );
    foreach (Vomit::IODevice *device, Vomit::IODevice::devices()) {
        vlog(VM_INITMSG, "%s at 0x%p", device->name(), device);
    }

    pic_init();
    dma_init();
    vga_init();
    fdc_init();
    ide_init();
    pit_init();
    busmouse_init();
    keyboard_init();
    gameport_init();
}

void vm_kill()
{
    vlog(VM_KILLMSG, "Killing VM");
    vga_kill();
    vomit_cpu_kill(&g_cpu);
}

void vm_exit(int exit_code)
{
    vm_kill();
    exit(exit_code);
}
