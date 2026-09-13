/*
 * Debug: Alt+S toggles a $68 HBL ISR that burns ~95% of each 512-cycle PAL line
 * so the machine crawls. TOS normally masks HBL (IPL 3); install drops IPL to 1.
 */

#ifndef ST_SLOW_HBL_H
#define ST_SLOW_HBL_H

#ifdef __cplusplus
extern "C" {
#endif

void ST_Slow_Hbl_Isr(void);
void ST_Slow_Hbl_Toggle(void);
void ST_Slow_Hbl_Remove(void);

#ifdef __cplusplus
}
#endif

#endif /* ST_SLOW_HBL_H */
