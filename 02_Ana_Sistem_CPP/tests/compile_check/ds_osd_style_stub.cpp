// Compile-check (test_phase5_callbacks_compile) icin minimal stub.
//
// track_selector.cpp, OSD stil yardimcilari ApplyStandardObjStyle /
// ResetDisplayMetaForAcquire'i cagirir; bunlarin gercek tanimi ds_app.cpp icinde.
// ds_app.cpp ise OpenCV CUDA + nvinfer bagimli oldugu icin CI test build'ine
// (SAVASAN_BUILD_APP=OFF) giremez. Bu hedefin amaci phase5_callbacks.cpp'nin
// derlenip LINK olmasini dogrulamak (alan-yeniden-adlandirma regresyonlari);
// OSD davranisi test edilmez. Bu yuzden no-op stub yeterli.

#include <gst/gst.h>

#include "nvdsmeta.h"
#include "gstnvdsmeta.h"

namespace savasan::deepstream {

void ApplyStandardObjStyle(NvDsObjectMeta* /*obj*/) {}
void ResetDisplayMetaForAcquire(NvDsDisplayMeta* /*display_meta*/) {}

}  // namespace savasan::deepstream
