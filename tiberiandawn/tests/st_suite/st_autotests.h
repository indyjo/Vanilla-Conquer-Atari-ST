#ifndef ST_AUTOTESTS_H
#define ST_AUTOTESTS_H

typedef enum {
	ST_AUTO_PASS = 0,
	ST_AUTO_FAIL = 1,
	ST_AUTO_SKIP = 2
} StAutotestStatus;

typedef struct {
	StAutotestStatus c2p;
	StAutotestStatus build_frame;
	StAutotestStatus audio;
	StAutotestStatus terrain_clip;
	StAutotestStatus st16_convert;
	unsigned c2p_checksum;
	int bf_ok;
	int bf_skip;
	int bf_fail;
	int terrain_clip_failures;
	int st16_convert_failures;
} StAutotestReport;

/* Runs C2P, Build_Frame (CONQUER.MIX SHPs), and one .AUD clip. Returns 0 if all PASS. */
int st_run_all_autotests(StAutotestReport *report);

#endif
