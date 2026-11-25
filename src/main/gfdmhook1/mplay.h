#ifndef GFDMHOOK1_MPLAY_H
#define GFDMHOOK1_MPLAY_H

/* Call on the boot thread before native boot starts the playback worker. */
void gfdm_mplay_apply_hooks(unsigned int version);

#endif
