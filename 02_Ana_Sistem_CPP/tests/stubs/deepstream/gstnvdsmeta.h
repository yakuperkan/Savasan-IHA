#ifndef SAVASAN_STUB_GSTNVDSMETA_H_
#define SAVASAN_STUB_GSTNVDSMETA_H_

#include <gst/gst.h>

#include "nvdsmeta.h"

#ifdef __cplusplus
extern "C" {
#endif

NvDsBatchMeta* gst_buffer_get_nvds_batch_meta(GstBuffer* buffer);

#ifdef __cplusplus
}
#endif

#endif  // SAVASAN_STUB_GSTNVDSMETA_H_
