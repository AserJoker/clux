#ifndef CLUX_THIRD_PARTY_ICU_DATA_H
#define CLUX_THIRD_PARTY_ICU_DATA_H

#ifdef __cplusplus
extern "C" {
#endif

/* Register the embedded ICU common data (icudt74l.dat).
 * Must be called once at program startup, before any ICU API is used.
 * Returns 0 on success, -1 on failure. */
int icu_data_init(void);

#ifdef __cplusplus
}
#endif

#endif /* CLUX_THIRD_PARTY_ICU_DATA_H */
