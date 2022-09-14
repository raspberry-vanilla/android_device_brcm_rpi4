/*
 * Copyright (C) 2019 The Android Open Source Project
 * Copyright (C) 2021-2022 KonstaKANG
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <signal.h>
#include <wakelock/wakelock.h>

int main() {
    auto wl = android::wakelock::WakeLock::tryGet("suspend_blocker_rpi");  // RAII object
    if (!wl.has_value()) {
        return EXIT_FAILURE;
    }

    sigset_t mask;
    sigemptyset(&mask);
    return sigsuspend(&mask);  // Infinite sleep
}
