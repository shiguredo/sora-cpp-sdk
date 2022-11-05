#ifndef LYRA_H_INCLUDED
#define LYRA_H_INCLUDED

#include <stddef.h>
#include <stdint.h>

#if defined(LYRA_EXPORTS)

#if defined(_WIN32)
#define DLL_INTERFACE __declspec(dllexport)
#else
#define DLL_INTERFACE
#endif

#else

#if defined(_WIN32)
#define DLL_INTERFACE __declspec(dllimport)
#else
#define DLL_INTERFACE
#endif

#endif

extern "C" {

struct lyra_encoder;
struct lyra_decoder;
struct lyra_vector_u8;
struct lyra_vector_s16;

DLL_INTERFACE lyra_encoder* lyra_encoder_create(int sample_rate_hz,
                                                int num_channels,
                                                int bitrate,
                                                bool enable_dtx,
                                                const char* model_path);
DLL_INTERFACE lyra_vector_u8* lyra_encoder_encode(lyra_encoder* encoder,
                                                  const int16_t* audio,
                                                  size_t length);
DLL_INTERFACE void lyra_encoder_destroy(lyra_encoder* encoder);

DLL_INTERFACE lyra_decoder* lyra_decoder_create(int sample_rate_hz,
                                                int num_channels,
                                                const char* model_path);
DLL_INTERFACE bool lyra_decoder_set_encoded_packet(lyra_decoder* decoder,
                                                   const uint8_t* encoded,
                                                   size_t length);
DLL_INTERFACE lyra_vector_s16* lyra_decoder_decode_samples(
    lyra_decoder* decoder,
    int num_samples);
DLL_INTERFACE void lyra_decoder_destroy(lyra_decoder* decoder);

DLL_INTERFACE size_t lyra_vector_u8_get_size(lyra_vector_u8* v);
DLL_INTERFACE uint8_t* lyra_vector_u8_get_data(lyra_vector_u8* v);
DLL_INTERFACE void lyra_vector_u8_destroy(lyra_vector_u8* v);

DLL_INTERFACE size_t lyra_vector_s16_get_size(lyra_vector_s16* v);
DLL_INTERFACE int16_t* lyra_vector_s16_get_data(lyra_vector_s16* v);
DLL_INTERFACE void lyra_vector_s16_destroy(lyra_vector_s16* v);
}

#endif