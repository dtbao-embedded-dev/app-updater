/**
 * @file    fw_config.h
 * @date    2026-09-07
 * @brief   Compile-time feature switches: which optional parts of the firmware
 *          this build carries.
 *
 * @copyright (c) 2026 dtbao. All rights reserved.
 */

#ifndef FW_CONFIG_H
#define FW_CONFIG_H

/* ------------------------------ Includes ------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------- Constants & macros ------------------------- */

/*
 * Set a switch to 0 and the feature is not called, so the linker leaves it out
 * of the image entirely. Nothing here is read at run time: these decide what
 * gets built, not what gets enabled, and there is no way to turn a feature on
 * in the field that was compiled out.
 *
 * **Where the switches are applied:** at the wiring points in
 * `application/app/src/app.c` and nowhere else. A module never tests its own
 * switch - `middleware/command` does not know whether the product wants a USB
 * channel, it only knows how to dispatch a frame. Keeping every `#if` in the
 * one file that composes the system is what stops these turning into the kind
 * of scattered conditional nobody can trace.
 *
 * **Where a second product's answers would go:** here is the default set, and
 * this repo has exactly one product, so this is also the only set. A second
 * product that wants a different answer gets its own header under
 * `workspace/<pid>/` with the workspace's include directory ahead of this one
 * - not an `#ifdef` on the product id in this file, which is how one file ends
 * up owning every product's decisions.
 *
 * The components stay in the build either way: a switch at 0 removes the calls,
 * not the compilation. That is deliberate - it keeps a disabled feature
 * compiling, so it cannot rot while it is off - and it costs nothing in the
 * image, which is what `tool-esp.py size` is for checking.
 */

/**
 * The USB command channel: CDC-ACM on USB-OTG, the frame codec, the dispatcher
 * and the task that drains the byte pipe.
 *
 * Off costs the ability to read a unit or push an image over a cable with no
 * network, which on this product is the entire bench and production path - so
 * off is for a variant that has another way in, not for saving space.
 *
 * Off gives back roughly 65.6 KB of static RAM: two 32788-byte frame buffers,
 * about 20.7 % of DRAM. It also gives the chip's single internal USB PHY back
 * to USB-Serial-JTAG, which means the console could move off UART0 again -
 * that is a `sdkconfig.defaults` decision, not something this switch does.
 */
#define FW_FEATURE_USB_COMMAND 1

/**
 * The scheduled HTTP update cycle: the state machine, its retry budget and the
 * image fetch it drives.
 *
 * Off costs unattended updates - the unit then only takes an image handed to
 * it over USB. Nothing else is affected: the settings, the boot-confirmation
 * and the rollback are not part of this switch, because a unit that cannot
 * update itself must still confirm the image it is running.
 */
#define FW_FEATURE_UPDATER 1

#ifdef __cplusplus
}
#endif

#endif /* FW_CONFIG_H */

/*** end of file ***/
