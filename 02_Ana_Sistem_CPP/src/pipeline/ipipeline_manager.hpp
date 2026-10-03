/**
 * @file ipipeline_manager.hpp
 * @brief Boru hattı (pipeline) yöneticisi arayüzü: GStreamer/DeepStream boru
 *        hattını kurma ve durumunu yönetme sözleşmesini tanımlar. Boru hattının
 *        oluşturulması, oynatılması ve durdurulması bu arayüz üzerinden soyutlanır.
 */
#ifndef SAVASAN_PIPELINE_IPIPELINE_MANAGER_HPP_
#define SAVASAN_PIPELINE_IPIPELINE_MANAGER_HPP_

#include <string>

namespace savasan::pipeline {

/// @brief Boru hattının yaşam döngüsü durumları.
enum class PipelineState {
  kNull = 0,     ///< Boru hattı kurulu değil / serbest (kaynaklar ayrılmamış).
  kReady = 1,    ///< Boru hattı hazır fakat henüz veri akıtmıyor.
  kPlaying = 2,  ///< Boru hattı aktif olarak veri işliyor (oynatma).
  kError = 3,    ///< Boru hattı hata durumunda.
};

/// @brief Boru hattı kurulum yapılandırması.
struct PipelineConfig {
  std::string pipeline_desc;  ///< GStreamer boru hattı tanım dizesi (element zinciri).
};

/// @brief Boru hattını kuran ve durumunu yöneten yöneticiler için saf sanal arayüz.
class IPipelineManager {
 public:
  virtual ~IPipelineManager() = default;

  /// @brief Verilen yapılandırmaya göre boru hattını oluşturur.
  /// @param cfg Boru hattı tanımını içeren yapılandırma.
  /// @return Kurulum başarılıysa true, aksi halde false.
  virtual bool BuildPipeline(const PipelineConfig& cfg) = 0;

  /// @brief Boru hattını istenen duruma geçirir.
  /// @param state Hedef boru hattı durumu.
  /// @return Geçiş başarılıysa true, aksi halde false.
  virtual bool SetState(PipelineState state) = 0;

  /// @brief Boru hattının mevcut durumunu döndürür.
  /// @return Geçerli boru hattı durumu.
  virtual PipelineState GetState() const = 0;
};

}  // namespace savasan::pipeline

#endif  // SAVASAN_PIPELINE_IPIPELINE_MANAGER_HPP_
