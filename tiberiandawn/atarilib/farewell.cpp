/*
	* farewell.cpp — greyscale dedication played on the way out of the main menu.
	*
	* The visible planar page is still the one allocated at startup. Scratch for the
	* glyph masks, particle state, and sine table lives on the stack. Timings below
	* are milliseconds at kVblHz. Coordinates, pens, and frame counts are short so
	* the 68000 does the effect in 16-bit arithmetic.
	*/

#ifdef ATARI_ST

#include "farewell.h"

#include "function.h"
#include "c2p.h"
#include "font.h"
#include "st_blit.h"
#include "st_temperat_palette.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Tunables                                                           */
/* ------------------------------------------------------------------ */

static const char kMessage[] = "Dedicated to Thorsten Otto.";

static const short kVblHz = 50;

/* Opening fade of the title picture, before the grey ramp is installed. */
static const short kBlackFadeMs = 800;

/* Letters reach 60% grey together at the end of kAppearMs. */
static const short kAppearMs = 2000;
static const short kAppearFadeMs = 800;
static const short kHoldMs = 2000;

/* Left-to-right start times of the dissolve are spread across this window. */
static const short kDissolveSpreadMs = 1200;
static const short kToWhiteMs = 500;
static const short kBodyFadeMs = 500;

/* Particle life is measured from the white peak. Hold is the still-white pause. */
static const short kParticleLifeMs = 2500;
static const short kParticleHoldMs = 400;
static const short kParticleNum = 1;
static const short kParticleDen = 8;
static const unsigned kParticleSeed = 0x5107E11Du;

/*
	* kSwayPx is the peak sideways distance in pixels. The flight starts on the
	* glyph pixel (offset 0) and walks kSwaySweep steps of the 64-step sine,
	* so 32 steps is out to kSwayPx and back to the stem.
	*/
static const short kTravelPx = 8;
static const short kSwayPx = 6;
static const short kSwaySweep = 40;

static const short kGreenFadeMs = 1000;
/* STE guns 0..15. Gun 10 green is the usual GEM desktop green. */
static const short kGreenR = 0;
static const short kGreenG = 10;
static const short kGreenB = 0;

static const short kGreyPen = 9;  /* 9/15 ≈ 60% */
static const short kWhitePen = 15;

/*
	* 320-wide volume text is TPF_6PT_GRAD | TPF_NOSHADOW, which sets FontXSpacing
	* to -2. Glyph boxes include the outline padding that spacing pulls back in.
	*/
static const short kGlyphGap = -2;
static const short kMaxGlyphs = 40;
static const short kCell = 16;
static const short kMaskBytes = (kCell * kCell) / 8;
static const short kMaxParticles = 192;

/* ------------------------------------------------------------------ */

struct FarewellRgb {
	short r;
	short g;
	short b;
};

struct FarewellScreen {
	unsigned char *base;
	short pitch;
	short width;
	short height;
};

struct FarewellGlyph {
	short x;
	short y;
	unsigned char w;
	unsigned char h;
	unsigned char ink[kMaskBytes];
	unsigned char part[kMaskBytes];
	signed char shown_pen;
	unsigned char shown_detached;
};

struct FarewellParticle {
	short ox;
	short oy;
	short px;
	short py;
	unsigned char phase;
	unsigned char last_pen;
	unsigned char drawn;
	unsigned char glyph;
};

static short Farewell_Ms_To_Vbl(short ms)
{
	int ticks;

	if (ms <= 0) {
		return 0;
	}
	/* ms * Hz exceeds a signed word (2500 * 50). The frame count does not. */
	ticks = (int)ms * kVblHz + 500;
	return (short)(ticks / 1000);
}

static short Farewell_Stagger(short index, short count, short spread)
{
	if (count <= 1 || spread <= 0 || index <= 0) {
		return 0;
	}
	if (index >= count) {
		index = (short)(count - 1);
	}
	return (short)(index * spread / (count - 1));
}

/*
	* Presses and mouse-button downs abort. Releases are ignored so the click
	* that chose Exit, once cleared, does not skip the effect on button-up.
	* Get() consumes the x/y words that follow a mouse event.
	*/
static short Farewell_Abort(void)
{
	if (!Keyboard) {
		return 0;
	}
	while (Keyboard->Check()) {
		unsigned key = (unsigned)Keyboard->Get();
		if (key & KN_RLSE_BIT) {
			continue;
		}
		return 1;
	}
	return 0;
}

static short Farewell_Unpack_Gun(unsigned word, short shift)
{
	unsigned nibble = (word >> shift) & 15u;
	return (short)(((nibble & 7u) << 1) | ((nibble >> 3) & 1u));
}

static short Farewell_Clamp_Gun(short v)
{
	if (v < 0) {
		return 0;
	}
	if (v > 15) {
		return 15;
	}
	return v;
}

static void Farewell_Write_Pen(short pen, short r, short g, short b)
{
	r = Farewell_Clamp_Gun(r);
	g = Farewell_Clamp_Gun(g);
	b = Farewell_Clamp_Gun(b);
	ST_HW_PALETTE_REGS[pen] = St_Pack_ST_HW_From_Rgb6(
		(unsigned char)((r * 63) / 15),
		(unsigned char)((g * 63) / 15),
		(unsigned char)((b * 63) / 15));
}

static void Farewell_Read_Palette(FarewellRgb rgb[16])
{
	for (short i = 0; i < 16; i++) {
		unsigned word = ST_HW_PALETTE_REGS[i];
		rgb[i].r = Farewell_Unpack_Gun(word, 8);
		rgb[i].g = Farewell_Unpack_Gun(word, 4);
		rgb[i].b = Farewell_Unpack_Gun(word, 0);
	}
}

/* Returns 1 if the player asked to leave. */
static short Farewell_Fade_To_Black(void)
{
	FarewellRgb from[16];
	short frames = Farewell_Ms_To_Vbl(kBlackFadeMs);

	Farewell_Read_Palette(from);
	if (frames <= 0) {
		for (short i = 0; i < 16; i++) {
			Farewell_Write_Pen(i, 0, 0, 0);
		}
		return Farewell_Abort();
	}

	for (short f = 1; f <= frames; f++) {
		if (Farewell_Abort()) {
			return 1;
		}
		short remain = (short)(frames - f);
		for (short i = 0; i < 16; i++) {
			Farewell_Write_Pen(i,
				(short)(from[i].r * remain / frames),
				(short)(from[i].g * remain / frames),
				(short)(from[i].b * remain / frames));
		}
		Wait_Vert_Blank();
	}
	return 0;
}

static void Farewell_Install_Grey(void)
{
	for (short i = 0; i < 16; i++) {
		Farewell_Write_Pen(i, i, i, i);
	}
}

/* Returns 1 if the player asked to leave. */
static short Farewell_Fade_To_Green(void)
{
	short frames = Farewell_Ms_To_Vbl(kGreenFadeMs);

	if (frames <= 0) {
		for (short i = 0; i < 16; i++) {
			Farewell_Write_Pen(i, kGreenR, kGreenG, kGreenB);
		}
		return Farewell_Abort();
	}

	for (short f = 1; f <= frames; f++) {
		if (Farewell_Abort()) {
			return 1;
		}
		for (short i = 0; i < 16; i++) {
			Farewell_Write_Pen(i,
				(short)(i + (kGreenR - i) * f / frames),
				(short)(i + (kGreenG - i) * f / frames),
				(short)(i + (kGreenB - i) * f / frames));
		}
		Wait_Vert_Blank();
	}
	return 0;
}

/* Inlined so a moving particle is a few stores, not a call into c2p.cpp. */
static void Farewell_Plot(const FarewellScreen *screen, short x, short y, short pen)
{
	unsigned char *p;
	short bitnum;
	unsigned char mask;
	unsigned char c;

	if (!screen->base || x < 0 || y < 0 || x >= screen->width || y >= screen->height || screen->pitch <= 0) {
		return;
	}
	p = screen->base + y * screen->pitch + (x >> 4) * 8 + ((x >> 3) & 1);
	bitnum = (short)(7 - (x & 7));
	mask = (unsigned char)(1u << bitnum);
	c = (unsigned char)(pen & 15);
	for (short pl = 0; pl < 4; pl++) {
		unsigned char *pb = p + pl * 2;
		if (c & (unsigned char)(1u << pl)) {
			*pb = (unsigned char)(*pb | mask);
		} else {
			*pb = (unsigned char)(*pb & (unsigned char)~mask);
		}
	}
}

static void Farewell_Bit_Set(unsigned char *mask, short bit)
{
	mask[bit >> 3] = (unsigned char)(mask[bit >> 3] | (unsigned char)(1u << (bit & 7)));
}

static short Farewell_Bit_Test(const unsigned char *mask, short bit)
{
	return (short)((mask[bit >> 3] >> (bit & 7)) & 1);
}

static unsigned short Farewell_Le16(const unsigned char *p)
{
	return (unsigned short)(p[0] | (p[1] << 8));
}

static void Farewell_Build_Sine(signed char sine[64])
{
	int quarter[17];

	for (short i = 0; i <= 16; i++) {
		int theta = i * 90 / 16;
		int span = theta * (180 - theta);
		int den = 40500 - span;
		quarter[i] = den ? (4 * span * 64) / den : 0;
	}
	quarter[0] = 0;
	quarter[16] = 64;

	for (short i = 0; i < 16; i++) {
		sine[i] = (signed char)quarter[i];
		sine[16 + i] = (signed char)quarter[16 - i];
		sine[32 + i] = (signed char)(-quarter[i]);
		sine[48 + i] = (signed char)(-quarter[16 - i]);
	}
}

static unsigned Farewell_Lcg(unsigned *rng)
{
	*rng = *rng * 1664525u + 1013904223u;
	return *rng;
}

static short Farewell_String_Width(const unsigned char *width_block, const char *text)
{
	short width = 0;
	short gap = 0;

	for (const char *s = text; *s; s++) {
		if (gap) {
			width = (short)(width + kGlyphGap);
		}
		gap = 1;
		width = (short)(width + width_block[(unsigned char)*s]);
	}
	return width;
}

/*
	* Volume labels use GRAD6FNT with TPF_USE_GRAD_PAL | TPF_NOSHADOW and CC_GREEN.
	* Nibbles 2 and 3 are the outline, and 4..10 are unused in that gradient row;
	* both are drawn as the background. Keeping them made every letter look fat.
	* Ink is nibble 1 and the green ramp 11..15.
	*/
static short Farewell_Nibble_Is_Ink(unsigned nibble)
{
	return (short)(nibble == 1 || nibble >= 11);
}

/*
	* A fixed-seed fraction of ink pixels is marked as a particle and omitted
	* from the body once the letter reaches white.
	*/
static void Farewell_Rasterize(
	FarewellGlyph *glyphs,
	short *glyph_count,
	FarewellParticle *particles,
	short *particle_count,
	short screen_w,
	short screen_h)
{
	const unsigned char *font;
	const unsigned char *info;
	const unsigned char *width_block;
	const unsigned char *offset_block;
	const unsigned char *height_block;
	short max_h;
	short text_w;
	short x;
	short y;
	unsigned rng = kParticleSeed;

	*glyph_count = 0;
	*particle_count = 0;

	font = (const unsigned char *)GradFont6Ptr;
	if (!font) {
		return;
	}

	info = font + Farewell_Le16(font + FONTINFOBLOCK);
	width_block = font + Farewell_Le16(font + FONTWIDTHBLOCK);
	offset_block = font + Farewell_Le16(font + FONTOFFSETBLOCK);
	height_block = font + Farewell_Le16(font + FONTHEIGHTBLOCK);
	max_h = info[FONTINFOMAXHEIGHT];
	if (max_h <= 0) {
		return;
	}
	if (max_h > kCell) {
		max_h = kCell;
	}

	text_w = Farewell_String_Width(width_block, kMessage);
	x = (short)((screen_w - text_w) / 2);
	y = (short)((screen_h - max_h) / 2);

	for (const char *s = kMessage; *s; s++) {
		unsigned char ch = (unsigned char)*s;
		short cw = width_block[ch];
		short top = height_block[ch * 2];
		short rows = height_block[ch * 2 + 1];
		const unsigned char *data = font + Farewell_Le16(offset_block + ch * 2);
		short low_nibble_next = 0;
		FarewellGlyph *glyph;
		short has_ink = 0;

		if (*glyph_count >= kMaxGlyphs) {
			break;
		}

		glyph = &glyphs[*glyph_count];
		memset(glyph, 0, sizeof(*glyph));
		glyph->x = x;
		glyph->y = y;
		glyph->w = (unsigned char)((cw > kCell) ? kCell : cw);
		glyph->h = (unsigned char)max_h;

		for (short row = 0; row < rows; row++) {
			short dy = (short)(top + row);
			for (short col = 0; col < cw; col++) {
				unsigned char nibble;
				if (low_nibble_next) {
					nibble = (unsigned char)((*data >> 4) & 0x0F);
					data++;
					low_nibble_next = 0;
				} else {
					nibble = (unsigned char)(*data & 0x0F);
					low_nibble_next = 1;
				}
				if (!Farewell_Nibble_Is_Ink(nibble) || dy < 0 || dy >= glyph->h || col >= kCell) {
					continue;
				}
				Farewell_Bit_Set(glyph->ink, (short)(dy * kCell + col));
				has_ink = 1;
				if (kParticleDen > 0 && (short)((Farewell_Lcg(&rng) >> 16) % (unsigned)kParticleDen) < kParticleNum
					&& *particle_count < kMaxParticles) {
					FarewellParticle *part = &particles[*particle_count];
					part->ox = (short)(x + col);
					part->oy = (short)(y + dy);
					part->px = part->ox;
					part->py = part->oy;
					part->phase = (unsigned char)((Farewell_Lcg(&rng) >> 8) & 63u);
					part->last_pen = 0;
					part->drawn = 0;
					part->glyph = (unsigned char)*glyph_count;
					Farewell_Bit_Set(glyph->part, (short)(dy * kCell + col));
					(*particle_count)++;
				}
			}
			if (low_nibble_next) {
				data++;
				low_nibble_next = 0;
			}
		}

		if (has_ink) {
			glyph->shown_pen = -1;
			glyph->shown_detached = 0;
			(*glyph_count)++;
		}
		x = (short)(x + cw + kGlyphGap);
	}
}

static short Farewell_Appear_Pen(short t, short fade)
{
	if (t < 0) {
		return 0;
	}
	if (fade <= 1 || t >= fade - 1) {
		return kGreyPen;
	}
	return (short)((kGreyPen * t) / (fade - 1));
}

static short Farewell_Body_Pen(short local, short to_white, short body)
{
	if (local < 0) {
		return kGreyPen;
	}
	if (local < to_white) {
		if (to_white <= 1) {
			return kWhitePen;
		}
		return (short)(kGreyPen + ((kWhitePen - kGreyPen) * local) / (to_white - 1));
	}
	{
		short u = (short)(local - to_white);
		if (body <= 1 || u >= body - 1) {
			return 0;
		}
		return (short)(kWhitePen - (kWhitePen * u) / (body - 1));
	}
}

static void Farewell_Stamp_Glyph(const FarewellScreen *screen, const FarewellGlyph *glyph, short pen, short detached)
{
	short w = glyph->w;
	short h = glyph->h;

	for (short row = 0; row < h; row++) {
		for (short col = 0; col < w; col++) {
			short bit = (short)(row * kCell + col);
			if (!Farewell_Bit_Test(glyph->ink, bit)) {
				continue;
			}
			if (detached && Farewell_Bit_Test(glyph->part, bit)) {
				continue;
			}
			Farewell_Plot(screen, (short)(glyph->x + col), (short)(glyph->y + row), pen);
		}
	}
}

static void Farewell_Particle_Pos(
	const FarewellParticle *part,
	short u,
	short move,
	const signed char *sine,
	short *x,
	short *y)
{
	short along = 0;
	short ang;
	short sway;

	if (move > 0) {
		if (u < 0) {
			u = 0;
		}
		if (u > move) {
			u = move;
		}
		along = u;
	}

	/*
		* Always start at sine index 0 so the first frame has no sideways
		* offset. phase only picks left or right; it must not slide the
		* sample onto a flat part of the wave, or kSwayPx never shows up
		* as motion.
		*/
	ang = (move > 1) ? (short)((along * kSwaySweep) / (move - 1)) : 0;
	sway = (short)(((short)sine[ang & 63] * kSwayPx) / 64);
	if (part->phase & 1) {
		sway = (short)(-sway);
	}
	*x = (short)(part->ox + sway);
	*y = (short)(part->oy - (move > 1 ? (kTravelPx * along) / (move - 1) : (move > 0 ? kTravelPx : 0)));
}

/* Body pen under a pixel another glyph still owns, else 0. Later glyphs win. */
static short Farewell_Cover_Pen(
	const FarewellGlyph *glyphs,
	const signed char *pens,
	const unsigned char *detached,
	short glyph_count,
	short x,
	short y)
{
	short found = 0;

	for (short g = 0; g < glyph_count; g++) {
		short col;
		short row;
		short bit;

		if (pens[g] <= 0) {
			continue;
		}
		col = (short)(x - glyphs[g].x);
		row = (short)(y - glyphs[g].y);
		if (col < 0 || row < 0 || col >= glyphs[g].w || row >= glyphs[g].h) {
			continue;
		}
		bit = (short)(row * kCell + col);
		if (!Farewell_Bit_Test(glyphs[g].ink, bit)) {
			continue;
		}
		if (detached[g] && Farewell_Bit_Test(glyphs[g].part, bit)) {
			continue;
		}
		found = pens[g];
	}
	return found;
}

static void Farewell_Present(unsigned char *front, unsigned char *back, short pitch, short y0, short y1)
{
	if (!front || !back || front == back || y1 <= y0 || pitch <= 0) {
		return;
	}
	ST_Blit_Linear_Copy(
		front + y0 * pitch,
		back + y0 * pitch,
		(unsigned long)(y1 - y0) * (unsigned long)pitch);
}

/* Returns 1 if the player asked to leave. */
static short Farewell_Play(
	const FarewellScreen *back,
	unsigned char *front,
	short band_y0,
	short band_y1,
	FarewellGlyph *glyphs,
	short glyph_count,
	FarewellParticle *particles,
	short particle_count,
	const signed char *sine)
{
	short appear = Farewell_Ms_To_Vbl(kAppearMs);
	short fade = Farewell_Ms_To_Vbl(kAppearFadeMs);
	short hold = Farewell_Ms_To_Vbl(kHoldMs);
	short spread = Farewell_Ms_To_Vbl(kDissolveSpreadMs);
	short to_white = Farewell_Ms_To_Vbl(kToWhiteMs);
	short body = Farewell_Ms_To_Vbl(kBodyFadeMs);
	short life = Farewell_Ms_To_Vbl(kParticleLifeMs);
	short particle_hold = Farewell_Ms_To_Vbl(kParticleHoldMs);
	short move;
	short hold_end;
	short dissolve_end;

	if (fade > appear) {
		fade = appear;
	}
	if (glyph_count <= 1) {
		fade = appear;
	}
	if (particle_hold > life) {
		particle_hold = life;
	}
	move = (short)(life - particle_hold);
	hold_end = (short)(appear + hold);
	/*
		* The last letter starts at `spread` into the dissolve, then spends
		* to-white plus the longer of the body fade and the particle life.
	*/
	{
		short tail = (short)(to_white + body);
		short particle_tail = (short)(to_white + life);
		if (particle_tail > tail) {
			tail = particle_tail;
		}
		dissolve_end = (short)(hold_end + spread + tail);
	}

	for (short frame = 0; frame < dissolve_end; frame++) {
		short dissolving = (short)(frame >= hold_end);
		signed char pens[kMaxGlyphs];
		unsigned char detached[kMaxGlyphs];
		short dirty = 0;

		if (Farewell_Abort()) {
			return 1;
		}

		for (short g = 0; g < glyph_count; g++) {
			short start = Farewell_Stagger(g, glyph_count, (short)(appear - fade));
			short pen = kGreyPen;
			short det = 0;

			if (!dissolving) {
				if (frame < appear) {
					pen = Farewell_Appear_Pen((short)(frame - start), fade);
				}
			} else {
				short local = (short)(frame - hold_end - Farewell_Stagger(g, glyph_count, spread));
				pen = Farewell_Body_Pen(local, to_white, body);
				det = (short)(local >= to_white);
			}
			pens[g] = (signed char)pen;
			detached[g] = (unsigned char)det;
		}

		/*
			* Lift particles that are about to move so a glyph restamp cannot
			* leave a hole, then paint only glyphs whose pen actually changed.
			*/
		for (short p = 0; p < particle_count; p++) {
			FarewellParticle *part = &particles[p];
			short local;
			short age;
			short x;
			short y;
			short pen;
			short cover;

			if (!dissolving || part->glyph >= glyph_count) {
				continue;
			}
			local = (short)(frame - hold_end - Farewell_Stagger(part->glyph, glyph_count, spread));
			if (local < to_white) {
				continue;
			}
			age = (short)(local - to_white);
			if (age >= life) {
				x = part->px;
				y = part->py;
				pen = 0;
			} else if (age < particle_hold) {
				x = part->ox;
				y = part->oy;
				pen = kWhitePen;
			} else {
				short u = (short)(age - particle_hold);
				Farewell_Particle_Pos(part, u, move, sine, &x, &y);
				pen = (move <= 1) ? 0 : (short)(kWhitePen - (kWhitePen * u) / (move - 1));
			}
			if (part->drawn && part->px == x && part->py == y && part->last_pen == (unsigned char)pen) {
				continue;
			}
			if (part->drawn) {
				cover = Farewell_Cover_Pen(glyphs, pens, detached, glyph_count, part->px, part->py);
				Farewell_Plot(back, part->px, part->py, cover);
				part->drawn = 0;
				dirty = 1;
			}
			if (pen > 0) {
				part->px = x;
				part->py = y;
			}
		}

		for (short g = 0; g < glyph_count; g++) {
			if (pens[g] == glyphs[g].shown_pen && detached[g] == glyphs[g].shown_detached) {
				continue;
			}
			if (pens[g] == 0 && glyphs[g].shown_pen < 0) {
				glyphs[g].shown_pen = 0;
				glyphs[g].shown_detached = detached[g];
				continue;
			}
			Farewell_Stamp_Glyph(back, &glyphs[g], pens[g], detached[g]);
			glyphs[g].shown_pen = pens[g];
			glyphs[g].shown_detached = detached[g];
			dirty = 1;
		}

		for (short p = 0; p < particle_count; p++) {
			FarewellParticle *part = &particles[p];
			short local;
			short age;
			short x;
			short y;
			short pen;

			if (!dissolving || part->glyph >= glyph_count) {
				continue;
			}
			local = (short)(frame - hold_end - Farewell_Stagger(part->glyph, glyph_count, spread));
			if (local < to_white) {
				continue;
			}
			age = (short)(local - to_white);
			if (age >= life || (age >= particle_hold && move <= 1)) {
				continue;
			}
			if (age < particle_hold) {
				x = part->ox;
				y = part->oy;
				pen = kWhitePen;
			} else {
				short u = (short)(age - particle_hold);
				Farewell_Particle_Pos(part, u, move, sine, &x, &y);
				pen = (short)(kWhitePen - (kWhitePen * u) / (move - 1));
			}
			if (part->drawn && part->px == x && part->py == y && part->last_pen == (unsigned char)pen) {
				continue;
			}
			Farewell_Plot(back, x, y, pen);
			part->px = x;
			part->py = y;
			part->last_pen = (unsigned char)pen;
			part->drawn = 1;
			dirty = 1;
		}

		/*
			* The back page is invisible. Copy the text band after the blank
			* so the beam is still above the line when the strip lands.
			*/
		Wait_Vert_Blank();
		if (dirty) {
			Farewell_Present(front, back->base, back->pitch, band_y0, band_y1);
		}
	}
	return 0;
}

void ST_Farewell(void)
{
	FarewellScreen back;
	unsigned char *front;
	FarewellGlyph glyphs[kMaxGlyphs];
	FarewellParticle particles[kMaxParticles];
	signed char sine[64];
	short glyph_count = 0;
	short particle_count = 0;
	short band_y0 = 0;
	short band_y1 = 0;

	if (Keyboard) {
		Keyboard->Clear();
	}

	if (Farewell_Fade_To_Black()) {
		return;
	}

	if (!VisiblePage.Is_ST_Planar() || !VisiblePage.Get_Buffer()) {
		return;
	}

	front = (unsigned char *)VisiblePage.Get_Buffer();
	back.base = front;
	back.pitch = (short)VisiblePage.Get_Pitch();
	back.width = (short)VisiblePage.Get_Width();
	back.height = (short)VisiblePage.Get_Height();
	if (back.pitch <= 0) {
		back.pitch = (short)(back.width / 2);
	}
	if (HiddenPage.Is_ST_Planar() && HiddenPage.Get_Buffer() && HiddenPage.Get_Buffer() != VisiblePage.Get_Buffer()
		&& HiddenPage.Get_Width() == back.width && HiddenPage.Get_Height() == back.height) {
		short hidden_pitch = (short)HiddenPage.Get_Pitch();
		if (hidden_pitch <= 0) {
			hidden_pitch = back.pitch;
		}
		if (hidden_pitch == back.pitch) {
			back.base = (unsigned char *)HiddenPage.Get_Buffer();
		}
	}

	VisiblePage.Clear(0);
	if (back.base != front) {
		HiddenPage.Clear(0);
	}
	Farewell_Install_Grey();
	Farewell_Build_Sine(sine);
	Farewell_Rasterize(glyphs, &glyph_count, particles, &particle_count, back.width, back.height);

	if (glyph_count > 0) {
		band_y0 = (short)(glyphs[0].y - kTravelPx);
		band_y1 = (short)(glyphs[0].y + glyphs[0].h + 1);
		for (short g = 1; g < glyph_count; g++) {
			short top = (short)(glyphs[g].y - kTravelPx);
			short bot = (short)(glyphs[g].y + glyphs[g].h + 1);
			if (top < band_y0) {
				band_y0 = top;
			}
			if (bot > band_y1) {
				band_y1 = bot;
			}
		}
		if (band_y0 < 0) {
			band_y0 = 0;
		}
		if (band_y1 > back.height) {
			band_y1 = back.height;
		}
		if (Farewell_Play(&back, front, band_y0, band_y1, glyphs, glyph_count, particles, particle_count, sine)) {
			return;
		}
	}

	Farewell_Fade_To_Green();
}

#else

#include "farewell.h"

void ST_Farewell(void)
{
}

#endif
