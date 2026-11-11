#pragma once

extern volatile int g_msc_busy;

void usb_init(void);
void usb_task(void);
int usb_host_connected(void);
void usb_disconnect(void);
