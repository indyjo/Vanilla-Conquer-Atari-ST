/*
 * st_blit.h - ST planar blit: shared register image, prepare, HW/SW backends, dispatch.
 */

#ifndef ST_BLIT_H
#define ST_BLIT_H

#include "c2p.h"
#include "function.h"

#include <stdint.h>

#ifdef __cplusplus
#include <cstddef>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ST_Blitter {
	int16_t src_x_inc;
	int16_t src_y_inc;
	void *src_addr;
	uint16_t endmask1;
	uint16_t endmask2;
	uint16_t endmask3;
	int16_t dst_x_inc;
	int16_t dst_y_inc;
	void *dst_addr;
	uint16_t x_count;
	uint16_t y_count;
	uint8_t hop;
	uint8_t op;
	uint8_t ctrl;
	uint8_t skew;
} ST_Blitter;

typedef struct ST_Blit_Job {
	const uint8_t *src_plane0;
	uint8_t *dst_plane0;
	bool src_addr_per_plane;
} ST_Blit_Job;

enum {
	ST_BLIT_CTRL_START = 0x80u,
	ST_BLIT_CTRL_START_HOG = 0xC0u
};

BOOL ST_Blit_Planar_Screen_Rect_Blit(
	const uint8_t *src_root,
	uint8_t *dst_root,
	int sx_abs,
	int sy_abs,
	int dx_abs,
	int dy_abs,
	int pixel_width,
	int pixel_height);

BOOL ST_Blit_Planar_Rect_Blit(
	const uint8_t *src_root,
	int src_row_bytes,
	int sx,
	int sy,
	uint8_t *dst_root,
	int dst_row_bytes,
	int dx,
	int dy,
	int pixel_width,
	int pixel_height);

BOOL ST_Blit_Planar_Rect_Blit_Or(
	const uint8_t *src_root,
	int src_row_bytes,
	int sx,
	int sy,
	uint8_t *dst_root,
	int dst_row_bytes,
	int dx,
	int dy,
	int pixel_width,
	int pixel_height);

BOOL ST_Blit_Mask_And_Planar_Rect(
	const uint8_t *mask_root,
	int mask_row_bytes,
	int sx,
	int sy,
	uint8_t *dst_root,
	int dst_row_bytes,
	int dx,
	int dy,
	int pixel_width,
	int pixel_height);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

class ST_Blit_Backend {
public:
	virtual ~ST_Blit_Backend() = default;

	volatile ST_Blitter &Regs() { return regs_; }

	/** Wait until the backend is idle (after the last kick of a multi-pass blit). */
	virtual void Await() = 0;
	/** Wait until idle, program src/dst/y_count, then kick one plane pass. */
	virtual void Execute(bool hog, uint16_t lines, void *src_addr, void *dst_addr) = 0;

	/**
	 * Run every bitplane of one prepared job. The hardware genuinely works a
	 * plane at a time, so the default is four Execute() passes; a software
	 * backend can override this to walk all four in one pass, where the planes
	 * of a 16-pixel column are 8 contiguous bytes.
	 */
	virtual void Run_Planes(const ST_Blit_Job &job, uint16_t lines, bool hog);

protected:
	explicit ST_Blit_Backend(volatile ST_Blitter &regs) : regs_(regs) {}

private:
	volatile ST_Blitter &regs_;
};

class ST_Blitter_Backend : public ST_Blit_Backend {
public:
	ST_Blitter_Backend();
	void Await() override;
	void Execute(bool hog, uint16_t lines, void *src_addr, void *dst_addr) override;
};

class ST_Soft_Backend : public ST_Blit_Backend {
public:
	ST_Soft_Backend();
	void Await() override;
	void Execute(bool hog, uint16_t lines, void *src_addr, void *dst_addr) override;
	void Run_Planes(const ST_Blit_Job &job, uint16_t lines, bool hog) override;

private:
	ST_Blitter state_{};
};

ST_Blitter_Backend &ST_Blit_HW_Backend();
ST_Soft_Backend &ST_Blit_Soft_Backend();

#endif /* __cplusplus */

#endif /* ST_BLIT_H */
