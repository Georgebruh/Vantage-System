#pragma once

#include <esp_sleep.h>

void rainBegin(esp_sleep_wakeup_cause_t cause);
void rainCollect();
void rainArmSleep();
