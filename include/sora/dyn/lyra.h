#ifndef DYN_LYRA_H_
#define DYN_LYRA_H_

// Lyra
#include <lyra.h>

#include "dyn.h"

namespace dyn {

#if defined(_WIN32)
static const char LYRA_SO[] = "lyra.dll";
#else
static const char LYRA_SO[] = "liblyra.so";
#endif
DYN_REGISTER(LYRA_SO, lyra_encoder_create);
DYN_REGISTER(LYRA_SO, lyra_encoder_encode);
DYN_REGISTER(LYRA_SO, lyra_encoder_destroy);
DYN_REGISTER(LYRA_SO, lyra_decoder_create);
DYN_REGISTER(LYRA_SO, lyra_decoder_set_encoded_packet);
DYN_REGISTER(LYRA_SO, lyra_decoder_decode_samples);
DYN_REGISTER(LYRA_SO, lyra_decoder_destroy);
DYN_REGISTER(LYRA_SO, lyra_vector_u8_get_size);
DYN_REGISTER(LYRA_SO, lyra_vector_u8_get_data);
DYN_REGISTER(LYRA_SO, lyra_vector_u8_destroy);
DYN_REGISTER(LYRA_SO, lyra_vector_s16_get_size);
DYN_REGISTER(LYRA_SO, lyra_vector_s16_get_data);
DYN_REGISTER(LYRA_SO, lyra_vector_s16_destroy);

}  // namespace dyn

#endif
