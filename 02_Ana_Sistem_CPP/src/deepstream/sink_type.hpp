/**
 * @file sink_type.hpp
 * @brief DeepStream boru hattı çıkış (sink) türü tanımı.
 */
#ifndef SAVASAN_DEEPSTREAM_SINK_TYPE_HPP_
#define SAVASAN_DEEPSTREAM_SINK_TYPE_HPP_

namespace savasan::deepstream {

/// @brief Boru hattı çıkış türü.
enum class SinkType {
  kFake,    ///< fakesink (görüntü gösterilmez; başlık/ölçüm koşuları).
  kDisplay  ///< Ekran sink'i (nv3dsink vb.).
};

}  // namespace savasan::deepstream

#endif  // SAVASAN_DEEPSTREAM_SINK_TYPE_HPP_
