#ifndef SAVASAN_STUB_NVDSMETA_H_
#define SAVASAN_STUB_NVDSMETA_H_

#include <glib.h>

typedef GList NvDsFrameMetaList;

typedef struct _NvDsBatchMeta {
  NvDsFrameMetaList* frame_meta_list;
} NvDsBatchMeta;

typedef struct _NvDsFrameMeta {
  guint pipeline_width;
  guint pipeline_height;
} NvDsFrameMeta;

#endif  // SAVASAN_STUB_NVDSMETA_H_
