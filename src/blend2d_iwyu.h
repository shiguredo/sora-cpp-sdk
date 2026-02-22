// Blend2D は <blend2d.h> だけを include するのを期待しているので、
// IWYU が変に include しないようにするために Blend2D のヘッダを
// export しておく

// IWYU pragma: begin_exports
#include <blend2d.h>
#include <blend2d/api.h>
#include <blend2d/array.h>
#include <blend2d/context.h>
#include <blend2d/font.h>
#include <blend2d/fontdata.h>
#include <blend2d/fontface.h>
#include <blend2d/format.h>
#include <blend2d/geometry.h>
#include <blend2d/image.h>
#include <blend2d/rgba.h>
// IWYU pragma: end_exports
