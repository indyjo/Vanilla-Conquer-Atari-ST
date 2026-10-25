/*
 * stvq_trace.c - Write an STVI decision trace. See stvq_trace.h for the layout.
 */
#include "stvq_trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct StviTrace {
	FILE *fp;
	uint64_t *off;
	uint32_t *snap;
	unsigned n, cap, nsnap, snap_cap;
	unsigned tiles_n, cb_entries, feat_len, frame_count;
	int err;
};

static int write_u16(FILE *fp, unsigned v)
{
	unsigned char b[2];
	b[0] = (unsigned char)(v & 255u);
	b[1] = (unsigned char)((v >> 8) & 255u);
	return fwrite(b, 1, 2, fp) == 2 ? 0 : -1;
}

static int write_u32(FILE *fp, unsigned v)
{
	unsigned char b[4];
	b[0] = (unsigned char)(v & 255u);
	b[1] = (unsigned char)((v >> 8) & 255u);
	b[2] = (unsigned char)((v >> 16) & 255u);
	b[3] = (unsigned char)((v >> 24) & 255u);
	return fwrite(b, 1, 4, fp) == 4 ? 0 : -1;
}

static int write_u64(FILE *fp, uint64_t v)
{
	unsigned char b[8];
	int i;
	for (i = 0; i < 8; i++)
		b[i] = (unsigned char)((v >> (8 * i)) & 255u);
	return fwrite(b, 1, 8, fp) == 8 ? 0 : -1;
}

static int write_f32(FILE *fp, float x)
{
	uint32_t u;
	memcpy(&u, &x, 4);
	return write_u32(fp, u);
}

static int write_f32n(FILE *fp, const float *p, unsigned n, unsigned stride)
{
	unsigned i;
	for (i = 0; i < n; i++) {
		if (write_f32(fp, p[i * stride]) != 0)
			return -1;
	}
	return 0;
}

StviTrace *stvi_trace_open(const char *path, const StviTraceInfo *info)
{
	StviTrace *tr;
	unsigned char hdr[64];
	unsigned i;

	if (!path || !path[0] || !info || !info->tiles_x || !info->tiles_y || !info->feat_len)
		return NULL;
	if (info->width > 0xffffu || info->height > 0xffffu || info->tiles_x > 0xffffu ||
	    info->tiles_y > 0xffffu || info->cb_entries > 0xffffu || info->feat_len > 0xffffu ||
	    info->y_coeffs > 0xffffu || info->chroma_coeffs > 0xffffu || info->fps > 0xffffu)
		return NULL;
	tr = (StviTrace *)calloc(1, sizeof(*tr));
	if (!tr)
		return NULL;
	tr->fp = fopen(path, "wb");
	if (!tr->fp) {
		free(tr);
		return NULL;
	}
	tr->tiles_n = info->tiles_x * info->tiles_y;
	tr->cb_entries = info->cb_entries;
	tr->feat_len = info->feat_len;
	tr->frame_count = info->frame_count;
	memset(hdr, 0, sizeof(hdr));
	memcpy(hdr, "STVI", 4);
	hdr[4] = (unsigned char)(STVI_VERSION & 255u);
	hdr[5] = (unsigned char)((STVI_VERSION >> 8) & 255u);
	hdr[6] = (unsigned char)(info->width & 255u);
	hdr[7] = (unsigned char)((info->width >> 8) & 255u);
	hdr[8] = (unsigned char)(info->height & 255u);
	hdr[9] = (unsigned char)((info->height >> 8) & 255u);
	hdr[10] = (unsigned char)(info->tiles_x & 255u);
	hdr[11] = (unsigned char)((info->tiles_x >> 8) & 255u);
	hdr[12] = (unsigned char)(info->tiles_y & 255u);
	hdr[13] = (unsigned char)((info->tiles_y >> 8) & 255u);
	hdr[14] = (unsigned char)(info->cb_entries & 255u);
	hdr[15] = (unsigned char)((info->cb_entries >> 8) & 255u);
	hdr[16] = (unsigned char)(info->feat_len & 255u);
	hdr[17] = (unsigned char)((info->feat_len >> 8) & 255u);
	hdr[18] = (unsigned char)(info->y_coeffs & 255u);
	hdr[19] = (unsigned char)((info->y_coeffs >> 8) & 255u);
	hdr[20] = (unsigned char)(info->chroma_coeffs & 255u);
	hdr[21] = (unsigned char)((info->chroma_coeffs >> 8) & 255u);
	hdr[22] = (unsigned char)(info->fps & 255u);
	hdr[23] = (unsigned char)((info->fps >> 8) & 255u);
	hdr[24] = (unsigned char)(info->frame_count & 255u);
	hdr[25] = (unsigned char)((info->frame_count >> 8) & 255u);
	hdr[26] = (unsigned char)((info->frame_count >> 16) & 255u);
	hdr[27] = (unsigned char)((info->frame_count >> 24) & 255u);
	memcpy(hdr + 28, &info->alpha, 4);
	memcpy(hdr + 32, &info->chroma_weight, 4);
	memcpy(hdr + 36, &info->gamma, 4);
	/* index_off at 40 stays 0 until close. Floats above are host-endian;
	 * rewrite them little-endian explicitly. */
	{
		uint32_t u;
		memcpy(&u, &info->alpha, 4);
		for (i = 0; i < 4; i++)
			hdr[28 + i] = (unsigned char)((u >> (8 * i)) & 255u);
		memcpy(&u, &info->chroma_weight, 4);
		for (i = 0; i < 4; i++)
			hdr[32 + i] = (unsigned char)((u >> (8 * i)) & 255u);
		memcpy(&u, &info->gamma, 4);
		for (i = 0; i < 4; i++)
			hdr[36 + i] = (unsigned char)((u >> (8 * i)) & 255u);
	}
	if (fwrite(hdr, 1, 64, tr->fp) != 64) {
		fclose(tr->fp);
		free(tr);
		return NULL;
	}
	return tr;
}

static int grow_off(StviTrace *tr)
{
	uint64_t *p;
	unsigned cap;
	if (tr->n < tr->cap)
		return 0;
	cap = tr->cap ? tr->cap * 2u : 64u;
	p = (uint64_t *)realloc(tr->off, cap * sizeof(*p));
	if (!p)
		return -1;
	tr->off = p;
	tr->cap = cap;
	return 0;
}

static int note_snap(StviTrace *tr, unsigned frame)
{
	uint32_t *p;
	unsigned cap;
	if (tr->nsnap < tr->snap_cap) {
		tr->snap[tr->nsnap++] = frame;
		return 0;
	}
	cap = tr->snap_cap ? tr->snap_cap * 2u : 8u;
	p = (uint32_t *)realloc(tr->snap, cap * sizeof(*p));
	if (!p)
		return -1;
	tr->snap = p;
	tr->snap_cap = cap;
	tr->snap[tr->nsnap++] = frame;
	return 0;
}

int stvi_trace_frame(StviTrace *tr, const StviFrameIn *in)
{
	long pos;
	unsigned i, ti;

	if (!tr || !tr->fp || tr->err || !in)
		return -1;
	if (in->nwin == 0 || in->nwin > 0xffffu || in->nrep > 0xffffu)
		return -1;
	if (!in->ste_rgb || !in->wins || !in->recon || !in->cb_index || !in->skip || !in->dist ||
	    !in->dist_n2 || !in->tile_feats)
		return -1;
	if (in->full && (!in->cb_valid || !in->cb_tiles || !in->cb_feats))
		return -1;
	if (in->nrep && !in->reps)
		return -1;
	if (grow_off(tr) != 0) {
		tr->err = 1;
		return -1;
	}
	pos = ftell(tr->fp);
	if (pos < 0) {
		tr->err = 1;
		return -1;
	}
	tr->off[tr->n++] = (uint64_t)pos;

	{
		unsigned flags = 0;
		if (in->full)
			flags |= STVI_F_FULL;
		if (in->win_pool && in->win_dist)
			flags |= STVI_F_WIN_RESIDUAL;
		if ((in->win_pool == NULL) != (in->win_dist == NULL)) {
			tr->err = 1;
			return -1;
		}
		if (write_u32(tr->fp, in->frame_index) != 0 || write_u16(tr->fp, in->segment) != 0 ||
		    write_u16(tr->fp, in->nwin) != 0 || write_u16(tr->fp, in->nrep) != 0 ||
		    write_u16(tr->fp, flags) != 0 || fwrite(in->ste_rgb, 1, 48, tr->fp) != 48) {
			tr->err = 1;
			return -1;
		}
	}
	for (i = 0; i < in->nwin; i++) {
		const StviWinImage *w = &in->wins[i];
		if (!w->pal768 || !w->src64) {
			tr->err = 1;
			return -1;
		}
		if (write_u16(tr->fp, w->seg) != 0 || write_u16(tr->fp, 0) != 0 ||
		    fwrite(w->pal768, 1, 768, tr->fp) != 768 ||
		    fwrite(w->src64, 1, (size_t)tr->tiles_n * 64u, tr->fp) != (size_t)tr->tiles_n * 64u) {
			tr->err = 1;
			return -1;
		}
	}
	if (fwrite(in->recon, 1, (size_t)tr->tiles_n * 32u, tr->fp) != (size_t)tr->tiles_n * 32u) {
		tr->err = 1;
		return -1;
	}
	if (in->full) {
		if (fwrite(in->cb_valid, 1, tr->cb_entries, tr->fp) != tr->cb_entries ||
		    fwrite(in->cb_tiles, 1, (size_t)tr->cb_entries * 32u, tr->fp) !=
		        (size_t)tr->cb_entries * 32u) {
			tr->err = 1;
			return -1;
		}
		for (i = 0; i < tr->cb_entries; i++) {
			if (write_f32n(tr->fp, in->cb_feats + (size_t)i * in->feat_stride, tr->feat_len, 1) != 0) {
				tr->err = 1;
				return -1;
			}
		}
		if (note_snap(tr, in->frame_index) != 0) {
			tr->err = 1;
			return -1;
		}
	}
	for (i = 0; i < in->nrep; i++) {
		const StviReplaceRec *r = &in->reps[i];
		unsigned char pad = 0;
		if (!r->tile || !r->feat) {
			tr->err = 1;
			return -1;
		}
		if (write_u16(tr->fp, r->index) != 0 || fwrite(&r->drawable, 1, 1, tr->fp) != 1 ||
		    fwrite(&pad, 1, 1, tr->fp) != 1 || fwrite(r->tile, 1, 32, tr->fp) != 32 ||
		    write_f32n(tr->fp, r->feat, tr->feat_len, 1) != 0) {
			tr->err = 1;
			return -1;
		}
	}
	for (ti = 0; ti < tr->tiles_n; ti++) {
		unsigned char pad = 0;
		unsigned idx = in->cb_index[ti];
		if (idx > 0xffffu) {
			tr->err = 1;
			return -1;
		}
		if (write_u16(tr->fp, idx) != 0 || fwrite(&in->skip[ti], 1, 1, tr->fp) != 1 ||
		    fwrite(&pad, 1, 1, tr->fp) != 1 || write_u32(tr->fp, in->dist[ti]) != 0 ||
		    write_u32(tr->fp, in->dist_n2[ti]) != 0 ||
		    write_f32n(tr->fp, in->tile_feats + (size_t)ti * tr->feat_len, tr->feat_len, 1) != 0) {
			tr->err = 1;
			return -1;
		}
	}
	if (in->win_pool && in->win_dist) {
		if (fwrite(in->win_pool, 1, in->nwin, tr->fp) != in->nwin) {
			tr->err = 1;
			return -1;
		}
		for (ti = 0; ti < in->nwin * tr->tiles_n; ti++) {
			if (write_u32(tr->fp, in->win_dist[ti]) != 0) {
				tr->err = 1;
				return -1;
			}
		}
	}
	return 0;
}

int stvi_trace_close(StviTrace *tr, int success)
{
	uint64_t index_off;
	unsigned i;
	int rc = 0;
	if (!tr)
		return 0;
	if (success && !tr->err && tr->fp) {
		long pos = ftell(tr->fp);
		if (pos < 0 || tr->n != tr->frame_count) {
			rc = -1;
		} else {
			index_off = (uint64_t)pos;
			for (i = 0; i < tr->n; i++) {
				if (write_u64(tr->fp, tr->off[i]) != 0)
					rc = -1;
			}
			if (write_u32(tr->fp, tr->nsnap) != 0)
				rc = -1;
			for (i = 0; i < tr->nsnap && rc == 0; i++) {
				if (write_u32(tr->fp, tr->snap[i]) != 0)
					rc = -1;
			}
			if (rc == 0) {
				unsigned char b[8];
				int k;
				if (fseek(tr->fp, 40, SEEK_SET) != 0)
					rc = -1;
				for (k = 0; k < 8; k++)
					b[k] = (unsigned char)((index_off >> (8 * k)) & 255u);
				if (rc == 0 && fwrite(b, 1, 8, tr->fp) != 8)
					rc = -1;
			}
		}
	}
	if (tr->fp && fclose(tr->fp) != 0)
		rc = -1;
	free(tr->off);
	free(tr->snap);
	free(tr);
	return rc;
}
