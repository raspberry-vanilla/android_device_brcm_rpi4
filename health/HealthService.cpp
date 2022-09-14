/*
 * Copyright (C) 2021-2022 KonstaKANG
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <health2/service.h>
#include <healthd/healthd.h>

int main() {
    return health_service_main();
}

void healthd_board_init(struct healthd_config*) {}

int healthd_board_battery_update(struct android::BatteryProperties* battery_props) {
    battery_props->chargerAcOnline = true;
    battery_props->batteryLevel = 100;
    return 0;
}
