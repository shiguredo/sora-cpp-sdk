#include "lyra.h"

#include "lyra_decoder.h"
#include "lyra_encoder.h"

using namespace chromemedia::codec;

extern "C" {

struct lyra_encoder {
  std::unique_ptr<LyraEncoder> encoder;
};
struct lyra_decoder {
  std::unique_ptr<LyraDecoder> decoder;
};
struct lyra_vector_u8 {
  std::vector<uint8_t> vec;
};
struct lyra_vector_s16 {
  std::vector<int16_t> vec;
};

lyra_encoder* lyra_encoder_create(int sample_rate_hz,
                                  int num_channels,
                                  int bitrate,
                                  bool enable_dtx,
                                  const char* model_path) {
  auto encoder = LyraEncoder::Create(sample_rate_hz, num_channels, bitrate,
                                     enable_dtx, model_path);
  if (encoder == nullptr) {
    return nullptr;
  }
  auto p = new lyra_encoder();
  p->encoder = std::move(encoder);
  return p;
}
lyra_vector_u8* lyra_encoder_encode(lyra_encoder* encoder,
                                    const int16_t* audio,
                                    size_t length) {
  auto r = encoder->encoder->Encode(absl::MakeConstSpan(audio, length));
  if (!r) {
    return nullptr;
  }
  auto p = new lyra_vector_u8();
  p->vec = std::move(*r);
  return p;
}
void lyra_encoder_destroy(lyra_encoder* encoder) {
  delete encoder;
}

lyra_decoder* lyra_decoder_create(int sample_rate_hz,
                                  int num_channels,
                                  const char* model_path) {
  auto decoder = LyraDecoder::Create(sample_rate_hz, num_channels, model_path);
  if (decoder == nullptr) {
    return nullptr;
  }
  auto p = new lyra_decoder();
  p->decoder = std::move(decoder);
  return p;
}
bool lyra_decoder_set_encoded_packet(lyra_decoder* decoder,
                                     const uint8_t* encoded,
                                     size_t length) {
  return decoder->decoder->SetEncodedPacket(
      absl::MakeConstSpan(encoded, length));
}
lyra_vector_s16* lyra_decoder_decode_samples(lyra_decoder* decoder,
                                             int num_samples) {
  auto r = decoder->decoder->DecodeSamples(num_samples);
  if (!r) {
    return nullptr;
  }
  auto p = new lyra_vector_s16();
  p->vec = std::move(*r);
  return p;
}
void lyra_decoder_destroy(lyra_decoder* decoder) {
  delete decoder;
}

size_t lyra_vector_u8_get_size(lyra_vector_u8* v) {
  return v->vec.size();
}
uint8_t* lyra_vector_u8_get_data(lyra_vector_u8* v) {
  return v->vec.data();
}
void lyra_vector_u8_destroy(lyra_vector_u8* v) {
  delete v;
}

size_t lyra_vector_s16_get_size(lyra_vector_s16* v) {
  return v->vec.size();
}
int16_t* lyra_vector_s16_get_data(lyra_vector_s16* v) {
  return v->vec.data();
}
void lyra_vector_s16_destroy(lyra_vector_s16* v) {
  delete v;
}
}
