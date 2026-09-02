#include <stdio.h>

#include "unicode/ubrk.h"
#include "unicode/uchar.h"
#include "unicode/ucnv.h"
#include "unicode/ustring.h"
#include "unicode/utypes.h"
#include "icu_data.h"

/* Segment a UTF-8 string into grapheme clusters and print each cluster's
 * UTF-16 code units, demonstrating that multi-codepoint sequences (ZWJ
 * families, skin-tone modifiers, regional-indicator flags) are one cluster. */
static void demo_grapheme(const char *utf8) {
  UChar buf[256];
  int32_t len = 0;
  UErrorCode status = U_ZERO_ERROR;

  u_strFromUTF8(buf, 256, &len, utf8, -1, &status);
  if (U_FAILURE(status)) {
    fprintf(stderr, "  utf8 conversion failed: %s\n", u_errorName(status));
    return;
  }

  UBreakIterator *bi = ubrk_open(UBRK_CHARACTER, "en", buf, len, &status);
  if (U_FAILURE(status)) {
    fprintf(stderr, "  ubrk_open failed: %s\n", u_errorName(status));
    ubrk_close(bi);
    return;
  }

  int cluster = 0;
  int32_t start = ubrk_first(bi);
  for (int32_t end = ubrk_next(bi); end != UBRK_DONE;
       start = end, end = ubrk_next(bi)) {
    cluster++;
    printf("  cluster %d: U+", cluster);
    for (int32_t i = start; i < end; i++) {
      printf("%04X ", (unsigned)buf[i]);
    }
    printf("(%d UTF-16 unit%s)\n", end - start,
           (end - start) == 1 ? "" : "s");
  }
  ubrk_close(bi);
  printf("  => \"%s\" = %d grapheme cluster(s)\n", utf8, cluster);
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  if (icu_data_init() != 0) {
    fprintf(stderr, "failed to initialize ICU common data\n");
    return 1;
  }

  /* Report the linked ICU library version. */
  UVersionInfo ver;
  u_getVersion(ver);
  char ver_str[U_MAX_VERSION_STRING_LENGTH];
  u_versionToString(ver, ver_str);
  printf("ICU version: %s\n", ver_str);

  /* Smoke test of the embedded common data: count available converters. */
  printf("available converters: %d\n", ucnv_countAvailable());

  printf("\nGrapheme cluster (emoji) segmentation:\n");

  /* Single base emoji: 1 cluster, 2 UTF-16 units (surrogate pair). */
  printf("Waving hand:\n");
  demo_grapheme("\xF0\x9F\x91\x8B"); /* U+1F44B */

  /* Base + skin-tone modifier: 1 cluster (4 UTF-16 units). */
  printf("Waving hand + dark skin tone:\n");
  demo_grapheme("\xF0\x9F\x91\x8B\xF0\x9F\x8F\xBF"); /* U+1F44B U+1F3FF */

  /* Regional indicators: 1 flag cluster (4 UTF-16 units). */
  printf("Flag (US):\n");
  demo_grapheme("\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8"); /* U+1F1FA U+1F1F8 */

  /* ZWJ family: 1 cluster of 5 astral chars (10 UTF-16 units). */
  printf("Family (adult+adult+girl), ZWJ sequence:\n");
  demo_grapheme("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7"); /* U+1F468 U+200D U+1F469 U+200D U+1F467 */

  /* Three base emoji separated by spaces: 3 clusters. */
  printf("Three separate emoji:\n");
  demo_grapheme("\xF0\x9F\x98\x80\x20\xF0\x9F\x98\x81\x20\xF0\x9F\x98\x82"); /* 😀 😁 😂 */

  printf("\nHello world\n");
  return 0;
}
