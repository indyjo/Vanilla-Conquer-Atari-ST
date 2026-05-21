#ifndef ST_C2P_AUTOTEST_H
#define ST_C2P_AUTOTEST_H

/* Returns number of failed checks. verbose: print per-check detail on success. */
int st_run_c2p_autotests_ex(int verbose, unsigned *out_checksum);

int st_run_c2p_autotests(void);

#endif
