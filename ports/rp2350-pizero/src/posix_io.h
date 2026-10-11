#pragma once

// Mounts the micro-SD card (FatFs) under /sd; false when there is no card.
bool dsp_sd_mount();
bool dsp_sd_mounted();
