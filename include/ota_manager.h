#pragma once

#include <Arduino.h>

// Over-the-air firmware update, triggered by the MQTT "OTA" command.
//
// applyUpdate() downloads the image over HTTPS, computes its SHA-256 while
// streaming it into the idle app partition, and only lets that partition
// become bootable if the digest matches the one the server sent. Any failure
// (bad hash, timeout, flash error) leaves the running firmware untouched.
// Runs on the MQTT task (core 0), never on the watchdog-registered loop task,
// so weighing carries on normally during the download.
//
// It does not reboot by itself: the caller asks health_monitor for a restart,
// which waits until the scale is idle so no weighing is cut off.
//
// The safety net against a bad build is the boot trial (checkBootRollback /
// confirmBootHealthy, see ota_rollback_logic.h): an OTA image that never gets
// itself onto MQTT within three boots is replaced by the previous one.
namespace OtaManager {

struct Command {
  char url[192];
  char sha256[65];  // 64 hex chars + NUL
  char version[32];
};

// Progress for the dashboard. stage: "accepted", "downloading", "retrying",
// "verifying", "failed". pct: 0..100, or -1 when not applicable.
// detail: short reason or version, never containing '"' or '\'.
using ProgressFn = void (*)(const char *stage, int pct, const char *detail);

// Blocks the calling task for up to ~90 s. Returns true once the new image is
// written, verified and set as the boot partition (it runs after the next
// reboot), false on any failure.
bool applyUpdate(const Command &command, ProgressFn progress = nullptr);

// First thing in setup(). Counts boots of an OTA image that has not yet
// confirmed itself; after too many, boots the last confirmed partition
// (does not return in that case).
void checkBootRollback();

// Once per boot, when the image has proven itself (MQTT status published
// after Config::BOOT_HEALTHY_AFTER_MS of uptime). Ends any boot trial and
// records the running partition as the rollback target for future updates.
void confirmBootHealthy();

// True while the running image is an OTA image still on trial.
bool onTrial();

// Download progress for the LCD: 0..100 while an update is being downloaded,
// -1 otherwise. Safe from any task.
int progressPercent();

// True once an image has been flashed and is waiting for the reboot.
bool updateStaged();

}  // namespace OtaManager
