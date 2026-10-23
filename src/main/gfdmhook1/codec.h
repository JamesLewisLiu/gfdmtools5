#ifndef GFDMHOOK1_CODEC_H
#define GFDMHOOK1_CODEC_H

/* Initialize before libmovie can create its DirectShow graph. */
void gfdm_codec_init(void);
void gfdm_codec_apply_hooks(void);

#endif
