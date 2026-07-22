/*
 * vqa_inspect.c - Walk a Westwood VQA and print structure / metadata.
 */
#include "vqa_inspect.h"

#include "vqa_format.h"
#include "vqa_io.h"

#ifdef __cplusplus
extern "C" {
#endif
int vqa_lcw_uncompress(void const *source, void *dest, unsigned length);
#ifdef __cplusplus
}
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct VqaChunkNote {
	off_t file_offset;
	uint32_t id;
	uint32_t size;
	int depth;
} VqaChunkNote;

typedef struct VqaFrameInfo {
	int index;
	off_t file_offset;
	uint32_t container_id;
	uint32_t container_size;
	int has_palette;
	int palette_compressed;
	uint32_t palette_hash;
	int has_snd;
	uint32_t snd_id;
	uint32_t snd_size;
	int has_codebook_full;
	int has_codebook_partial;
	int has_vpt;
	uint32_t vpt_id;
} VqaFrameInfo;

typedef struct VqaInspectCtx {
	VqaReader *r;
	const VqaInspectOpts *opts;
	VqaHeader hdr;
	char name[256];
	int has_name;
	uint32_t *finf_offsets;
	unsigned finf_count;
	VqaFrameInfo *frames;
	unsigned frame_count;
	VqaChunkNote *chunks;
	unsigned chunk_count;
	unsigned chunk_cap;
	uint32_t cur_palette_hash;
	int have_palette_hash;
} VqaInspectCtx;

static uint32_t vqa_hash_fnv1a(const unsigned char *data, size_t len)
{
	uint32_t h = 2166136261u;
	size_t i;

	for (i = 0; i < len; i++) {
		h ^= data[i];
		h *= 16777619u;
	}
	return h;
}

static int vqa_note_chunk(VqaInspectCtx *ctx, off_t offset, uint32_t id, uint32_t size, int depth)
{
	if (!ctx->opts->verbose) {
		return 0;
	}
	if (ctx->chunk_count == ctx->chunk_cap) {
		unsigned ncap = ctx->chunk_cap ? ctx->chunk_cap * 2u : 64u;
		VqaChunkNote *n = (VqaChunkNote *)realloc(ctx->chunks, ncap * sizeof(*n));
		if (!n) {
			fprintf(stderr, "error: out of memory\n");
			return -1;
		}
		ctx->chunks = n;
		ctx->chunk_cap = ncap;
	}
	ctx->chunks[ctx->chunk_count].file_offset = offset;
	ctx->chunks[ctx->chunk_count].id = id;
	ctx->chunks[ctx->chunk_count].size = size;
	ctx->chunks[ctx->chunk_count].depth = depth;
	ctx->chunk_count++;
	return 0;
}

static int vqa_palette_from_cplz(VqaReader *r, uint32_t comp_size, unsigned char *out768)
{
	unsigned char work[VQA_LCW_PAL_WORK];
	size_t padded = (size_t)vqa_iff_data_padded(comp_size);

	if (padded > sizeof(work)) {
		fprintf(stderr, "error: CPLZ chunk too large (%u)\n", comp_size);
		return -1;
	}
	if (vqa_reader_read(r, work + (sizeof(work) - padded), padded) != 0) {
		return -1;
	}
	memset(out768, 0, VQA_PALETTE_BYTES);
	if (vqa_lcw_uncompress(work + (sizeof(work) - padded), out768, VQA_PALETTE_BYTES) <= 0) {
		fprintf(stderr, "error: LCW palette decompress failed\n");
		return -1;
	}
	vqa_sanitize_vga6_palette(out768);
	return 0;
}

static int vqa_handle_palette_chunk(
    VqaInspectCtx *ctx, VqaFrameInfo *frame, uint32_t id, uint32_t size, VqaReader *r)
{
	unsigned char pal[VQA_PALETTE_BYTES];
	uint32_t hash;

	frame->has_palette = 1;
	if (id == VQA_CHUNK_CPLZ) {
		frame->palette_compressed = 1;
		if (vqa_palette_from_cplz(r, size, pal) != 0) {
			return -1;
		}
	} else {
		unsigned want = size < VQA_PALETTE_BYTES ? size : VQA_PALETTE_BYTES;
		unsigned padded = vqa_iff_data_padded(size);

		if (vqa_reader_read(r, pal, want) != 0) {
			return -1;
		}
		if (want < VQA_PALETTE_BYTES) {
			memset(pal + want, 0, VQA_PALETTE_BYTES - want);
		}
		if (padded > want) {
			if (vqa_reader_skip(r, padded - want) != 0) {
				return -1;
			}
		}
		vqa_sanitize_vga6_palette(pal);
	}

	hash = vqa_hash_fnv1a(pal, VQA_PALETTE_BYTES);
	frame->palette_hash = hash;
	ctx->cur_palette_hash = hash;
	ctx->have_palette_hash = 1;
	return 0;
}

static int vqa_scan_frame_body(VqaInspectCtx *ctx, VqaFrameInfo *frame, uint32_t body_size, int depth)
{
	VqaReader *r = ctx->r;
	unsigned consumed = 0;

	while (consumed < body_size) {
		off_t off = vqa_reader_tell(r);
		uint32_t id;
		uint32_t size;
		unsigned padded;

		if (vqa_reader_read_chunk_hdr(r, &id, &size) != 0) {
			return -1;
		}
		padded = vqa_iff_data_padded(size);
		if (consumed + 8u + padded > body_size) {
			fprintf(stderr,
			    "error: frame %d: nested chunks exceed container (%u > %u)\n",
			    frame->index,
			    consumed + 8u + padded,
			    body_size);
			return -1;
		}
		consumed += 8u + padded;
		if (vqa_note_chunk(ctx, off, id, size, depth) != 0) {
			return -1;
		}

		switch (id) {
		case VQA_CHUNK_CPL0:
		case VQA_CHUNK_CPLZ:
			if (vqa_handle_palette_chunk(ctx, frame, id, size, r) != 0) {
				return -1;
			}
			break;
		case VQA_CHUNK_CBF0:
		case VQA_CHUNK_CBFZ:
			frame->has_codebook_full = 1;
			if (vqa_reader_skip(r, padded) != 0) {
				return -1;
			}
			break;
		case VQA_CHUNK_CBP0:
		case VQA_CHUNK_CBPZ:
			frame->has_codebook_partial = 1;
			if (vqa_reader_skip(r, padded) != 0) {
				return -1;
			}
			break;
		case VQA_CHUNK_VPT0:
		case VQA_CHUNK_VPTZ:
		case VQA_CHUNK_VPTR:
		case VQA_CHUNK_VPTD:
		case VQA_CHUNK_VPTK:
		case VQA_CHUNK_VPRZ:
		case VQA_CHUNK_VPDZ:
		case VQA_CHUNK_VPKZ:
			frame->has_vpt = 1;
			frame->vpt_id = id;
			if (vqa_reader_skip(r, padded) != 0) {
				return -1;
			}
			break;
		case VQA_CHUNK_SND0:
		case VQA_CHUNK_SND1:
		case VQA_CHUNK_SND2:
		case VQA_CHUNK_SNA0:
		case VQA_CHUNK_SNA1:
		case VQA_CHUNK_SNA2:
			frame->has_snd = 1;
			frame->snd_id = id;
			frame->snd_size = size;
			if (vqa_reader_skip(r, padded) != 0) {
				return -1;
			}
			break;
		case VQA_CHUNK_SN2J:
			if (vqa_reader_skip(r, padded) != 0) {
				return -1;
			}
			break;
		default:
			if (vqa_reader_skip(r, padded) != 0) {
				return -1;
			}
			break;
		}
	}
	return 0;
}

static int vqa_is_frame_container(uint32_t id)
{
	return id == VQA_CHUNK_VQFR || id == VQA_CHUNK_VQFL || id == VQA_CHUNK_VQFK;
}

static int vqa_add_frame(VqaInspectCtx *ctx, off_t offset, uint32_t id, uint32_t size)
{
	VqaFrameInfo *f;

	ctx->frames = (VqaFrameInfo *)realloc(ctx->frames, (ctx->frame_count + 1u) * sizeof(*ctx->frames));
	if (!ctx->frames) {
		fprintf(stderr, "error: out of memory\n");
		return -1;
	}
	f = &ctx->frames[ctx->frame_count];
	memset(f, 0, sizeof(*f));
	f->index = (int)ctx->frame_count;
	f->file_offset = offset;
	f->container_id = id;
	f->container_size = size;
	ctx->frame_count++;
	return 0;
}

static int vqa_scan_chunk(VqaInspectCtx *ctx, uint32_t id, uint32_t size, int depth)
{
	VqaReader *r = ctx->r;
	off_t off = vqa_reader_tell(r) - 8;
	unsigned padded = vqa_iff_data_padded(size);

	if (vqa_note_chunk(ctx, off, id, size, depth) != 0) {
		return -1;
	}

	if (id == VQA_CHUNK_VQHD) {
		unsigned char raw[VQA_VQHD_SIZE];
		if (size != VQA_VQHD_SIZE) {
			fprintf(stderr, "error: VQHD size %u (expected %u)\n", size, VQA_VQHD_SIZE);
			return -1;
		}
		if (vqa_reader_read(r, raw, sizeof(raw)) != 0) {
			return -1;
		}
		ctx->hdr.version = vqa_read_le16(raw + 0);
		ctx->hdr.flags = vqa_read_le16(raw + 2);
		ctx->hdr.frames = vqa_read_le16(raw + 4);
		ctx->hdr.image_width = vqa_read_le16(raw + 6);
		ctx->hdr.image_height = vqa_read_le16(raw + 8);
		ctx->hdr.block_width = raw[10];
		ctx->hdr.block_height = raw[11];
		ctx->hdr.fps = raw[12];
		ctx->hdr.groupsize = raw[13];
		ctx->hdr.num1_colors = vqa_read_le16(raw + 14);
		ctx->hdr.cb_entries = vqa_read_le16(raw + 16);
		ctx->hdr.xpos = vqa_read_le16(raw + 18);
		ctx->hdr.ypos = vqa_read_le16(raw + 20);
		ctx->hdr.max_framesize = vqa_read_le16(raw + 22);
		ctx->hdr.sample_rate = vqa_read_le16(raw + 24);
		ctx->hdr.channels = raw[26];
		ctx->hdr.bits_per_sample = raw[27];
		ctx->hdr.alt_sample_rate = vqa_read_le16(raw + 28);
		ctx->hdr.alt_channels = raw[30];
		ctx->hdr.alt_bits_per_sample = raw[31];
		ctx->hdr.color_mode = raw[32];
		ctx->hdr.field_21 = raw[33];
		ctx->hdr.max_compressed_cb_size = vqa_read_le32(raw + 34);
		ctx->hdr.field_26 = vqa_read_le32(raw + 38);
		return 0;
	}

	if (id == VQA_CHUNK_NAME) {
		size_t n = size < sizeof(ctx->name) - 1u ? size : sizeof(ctx->name) - 1u;
		if (vqa_reader_read(r, ctx->name, n) != 0) {
			return -1;
		}
		ctx->name[n] = '\0';
		ctx->has_name = 1;
		if (padded > n) {
			if (vqa_reader_skip(r, padded - n) != 0) {
				return -1;
			}
		}
		return 0;
	}

	if (id == VQA_CHUNK_FINF) {
		unsigned count = padded / 4u;
		unsigned i;
		ctx->finf_offsets = (uint32_t *)malloc(count * sizeof(uint32_t));
		if (!ctx->finf_offsets) {
			fprintf(stderr, "error: out of memory\n");
			return -1;
		}
		ctx->finf_count = count;
		for (i = 0; i < count; i++) {
			unsigned char le[4];
			if (vqa_reader_read(r, le, 4) != 0) {
				return -1;
			}
			ctx->finf_offsets[i] = vqa_read_le32(le);
		}
		if (padded > count * 4u) {
			if (vqa_reader_skip(r, padded - count * 4u) != 0) {
				return -1;
			}
		}
		return 0;
	}

	if (vqa_is_frame_container(id)) {
		VqaFrameInfo *frame;
		if (vqa_add_frame(ctx, off, id, size) != 0) {
			return -1;
		}
		frame = &ctx->frames[ctx->frame_count - 1u];
		return vqa_scan_frame_body(ctx, frame, size, depth + 1);
	}

	return vqa_reader_skip(r, padded);
}

static int vqa_walk_iff(VqaInspectCtx *ctx, off_t end_pos, int depth)
{
	VqaReader *r = ctx->r;

	while (vqa_reader_tell(r) < end_pos) {
		uint32_t id;
		uint32_t size;
		if (vqa_reader_read_chunk_hdr(r, &id, &size) != 0) {
			return -1;
		}
		if (vqa_scan_chunk(ctx, id, size, depth) != 0) {
			return -1;
		}
	}
	return 0;
}

static void vqa_print_header(const VqaInspectCtx *ctx, const char *path)
{
	char flags[128];
	const VqaHeader *h = &ctx->hdr;
	unsigned blocks_w = h->block_width ? (unsigned)h->image_width / h->block_width : 0;
	unsigned blocks_h = h->block_height ? (unsigned)h->image_height / h->block_height : 0;

	vqa_format_flags(h->flags, flags, sizeof(flags));

	printf("file: %s\n", path);
	printf("size: %lld bytes\n", (long long)ctx->r->size);
	if (ctx->has_name) {
		printf("name: %s\n", ctx->name);
	}
	printf("version: %u\n", (unsigned)h->version);
	printf("flags: 0x%04x (%s)\n", (unsigned)h->flags, flags);
	printf("frames: header=%u scanned=%u\n", (unsigned)h->frames, ctx->frame_count);
	printf("image: %ux%u at (%u,%u)\n",
	    (unsigned)h->image_width,
	    (unsigned)h->image_height,
	    (unsigned)h->xpos,
	    (unsigned)h->ypos);
	printf("block: %ux%u (%u x %u cells)\n",
	    (unsigned)h->block_width,
	    (unsigned)h->block_height,
	    blocks_w,
	    blocks_h);
	printf("fps: %u\n", (unsigned)h->fps);
	printf("groupsize: %u\n", (unsigned)(h->groupsize ? h->groupsize : 8));
	printf("colors: num1=%u color_mode=%s (%u)\n",
	    (unsigned)h->num1_colors,
	    vqa_color_mode_name(h->color_mode),
	    (unsigned)h->color_mode);
	printf("codebook: %u entries, max_compressed_cb=%u\n",
	    (unsigned)h->cb_entries,
	    (unsigned)h->max_compressed_cb_size);
	printf("max_frame_chunk: %u bytes\n", (unsigned)h->max_framesize);
	if (h->flags & 1u) {
		printf("audio: %u Hz, %u ch, %u-bit",
		    (unsigned)h->sample_rate,
		    (unsigned)h->channels,
		    (unsigned)h->bits_per_sample);
		if (h->flags & 2u) {
			printf("  (alt %u Hz, %u ch, %u-bit)",
			    (unsigned)h->alt_sample_rate,
			    (unsigned)h->alt_channels,
			    (unsigned)h->alt_bits_per_sample);
		}
		printf("\n");
	} else {
		printf("audio: none\n");
	}
	if (ctx->finf_count) {
		printf("finf: %u offset entries\n", ctx->finf_count);
	}
}

static void vqa_print_frame_line(const VqaFrameInfo *f, int palette_changed)
{
	printf("frame %4d  off=%8lld  %s size=%5u",
	    f->index,
	    (long long)f->file_offset,
	    vqa_chunk_name(f->container_id),
	    f->container_size);
	if (f->has_palette) {
		printf("  pal=%s hash=%08x",
		    f->palette_compressed ? "CPLZ" : "CPL0",
		    f->palette_hash);
		if (palette_changed) {
			printf(" *");
		}
	}
	if (f->has_codebook_full) {
		printf("  cb=full");
	}
	if (f->has_codebook_partial) {
		printf("  cb=part");
	}
	if (f->has_vpt) {
		printf("  vpt=%s", vqa_chunk_name(f->vpt_id));
	}
	if (f->has_snd) {
		printf("  snd=%s:%u", vqa_chunk_name(f->snd_id), f->snd_size);
	}
	printf("\n");
}

static void vqa_print_verbose_chunks(const VqaInspectCtx *ctx)
{
	unsigned i;
	printf("\nchunks:\n");
	for (i = 0; i < ctx->chunk_count; i++) {
		const VqaChunkNote *c = &ctx->chunks[i];
		int d;
		printf("  off=%8lld", (long long)c->file_offset);
		for (d = 0; d < c->depth; d++) {
			printf("  ");
		}
		printf("%s  size=%u\n", vqa_chunk_name(c->id), c->size);
	}
}

static void vqa_free_ctx(VqaInspectCtx *ctx)
{
	free(ctx->finf_offsets);
	free(ctx->frames);
	free(ctx->chunks);
}

int vqa_inspect_file(const char *path, const VqaInspectOpts *opts)
{
	VqaReader reader;
	VqaInspectCtx ctx;
	unsigned char form_hdr[12];
	uint32_t form_size;
	off_t form_end;
	unsigned i;
	uint32_t prev_hash = 0;
	int have_prev = 0;

	memset(&ctx, 0, sizeof(ctx));
	ctx.r = &reader;
	ctx.opts = opts;

	if (vqa_reader_open(&reader, path) != 0) {
		return 1;
	}

	if (vqa_reader_read(&reader, form_hdr, sizeof(form_hdr)) != 0) {
		vqa_reader_close(&reader);
		return 1;
	}
	if (vqa_read_le32(form_hdr) != VQA_CHUNK_FORM || vqa_read_le32(form_hdr + 8) != VQA_CHUNK_WVQA) {
		fprintf(stderr, "error: %s: not a FORM/WVQA file\n", path);
		vqa_reader_close(&reader);
		return 1;
	}
	form_size = vqa_read_be32(form_hdr + 4);
	form_end = 8 + (off_t)form_size;

	if (vqa_walk_iff(&ctx, form_end, 0) != 0) {
		vqa_free_ctx(&ctx);
		vqa_reader_close(&reader);
		return 1;
	}

	vqa_print_header(&ctx, path);

	if (opts->show_palette_changes || opts->show_frames) {
		printf("\n");
		if (opts->show_palette_changes && !opts->show_frames) {
			printf("palette changes:\n");
		} else {
			printf("frames:\n");
		}
	}

	for (i = 0; i < ctx.frame_count; i++) {
		VqaFrameInfo *f = &ctx.frames[i];
		int palette_changed = 0;

		if (f->has_palette) {
			if (!have_prev || f->palette_hash != prev_hash) {
				palette_changed = 1;
			}
			prev_hash = f->palette_hash;
			have_prev = 1;
		}

		if (opts->show_palette_changes && palette_changed) {
			printf("palette change at frame %d (offset %lld, hash %08x)\n",
			    f->index,
			    (long long)f->file_offset,
			    f->palette_hash);
		}
		if (opts->show_frames) {
			vqa_print_frame_line(f, palette_changed);
		}
	}

	if (opts->verbose) {
		vqa_print_verbose_chunks(&ctx);
	}

	if (ctx.hdr.frames && ctx.frame_count != ctx.hdr.frames) {
		fprintf(stderr,
		    "warning: VQHD frame count (%u) differs from scanned frame containers (%u)\n",
		    (unsigned)ctx.hdr.frames,
		    ctx.frame_count);
	}

	vqa_free_ctx(&ctx);
	vqa_reader_close(&reader);
	return 0;
}
