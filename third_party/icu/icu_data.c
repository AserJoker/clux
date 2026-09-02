#include "icu_data.h"
#include "unicode/udata.h"
#include "unicode/utypes.h"

/* Generated C byte array from the embedded icudt*.dat (see third_party/icu/CMakeLists.txt).
   The symbol name is version-independent so the same code works across ICU versions. */
extern const unsigned char icu_embedded_dat[];
extern const size_t icu_embedded_dat_size;

int icu_data_init(void) {
  UErrorCode status = U_ZERO_ERROR;
  udata_setCommonData(icu_embedded_dat, &status);
  return U_FAILURE(status) ? -1 : 0;
}
