#ifndef GFDMHOOK1_BACKUP_H
#define GFDMHOOK1_BACKUP_H

/* V4 re-latches the first-start notice after TEST; V5-V8 already clear it. */
void gfdm_backup_apply_hooks(unsigned int version);

#endif
