//
// Atari / POSIX: Load_Title_Screen + Read_PCX_File (moved from WINSTUB.CPP so the ST test
// harness can link the same implementation as the game).
//

#include "function.h"
#include "filepcx.h"
#include "c2p.h"
#include "gbuffer.h"
#include "palette.h"

#define POOL_SIZE 2048

#pragma pack(push, 1)
typedef struct {
	unsigned char red;
	unsigned char green;
	unsigned char blue;
} PG_RGB;
#pragma pack(pop)
static_assert(sizeof(PG_RGB) == 3, "PG_RGB must be 3 bytes for PCX palette");

static inline short SwapLE16(short val)
{
#ifdef BIG_ENDIAN
	return (short)(((unsigned short)(val) << 8) | ((unsigned short)(val) >> 8));
#else
	return val;
#endif
}

static void Title_TryInstallC2PWeights(void)
{
	C2P_WeightSet weight_set;
	CCFileClass file("TITLE.W16");
	long got;

	C2P_Clear_CustomWeights();
	if (file.Is_Available() && file.Open(READ)) {
		got = file.Read(&weight_set, (long)sizeof(weight_set));
		file.Close();
		if (got == (long)sizeof(weight_set) && C2P_Install_CustomWeights(&weight_set)) {
			return;
		}
	}
	C2P_Select_WeightSet(C2P_WEIGHTSET_HTITLE);
}

void Load_Title_Screen(char const *name, GraphicViewPortClass *video_page, unsigned char *palette)
{
	if (!name || !video_page || strcmp(name, "TITLE.CPS") != 0) {
#ifdef ATARI_ST
		if (name) {
			printf("C&C ST - Load_Title_Screen skipped (name=%s).\n", name);
		}
#endif
		return;
	}

	CCFileClass file_obj(name);
	if (!file_obj.Is_Available()) {
#ifdef ATARI_ST
		printf("C&C ST - %s not available (need CONQUER.MIX in cwd).\n", name);
#endif
		return;
	}
	Title_TryInstallC2PWeights();
	int uncomp_size = Load_Uncompress(file_obj, SysMemPage, SysMemPage, palette);
#ifdef ATARI_ST
	if (uncomp_size <= 0) {
		printf("C&C ST - Load_Uncompress failed for %s.\n", name);
		return;
	}
#endif
	if (palette) {
		Set_Palette(palette);
	}

	int src_w = 320;
	int src_h = 200;
	int dst_w = video_page->Get_Width();
	int dst_h = video_page->Get_Height();
	GraphicBufferClass *dst_gb = video_page->Get_Graphic_Buffer();
	bool title_drawn = false;
	if (dst_gb && dst_gb->Is_ST_Planar() && src_w == 320 && src_h == 200
		&& dst_w == 320 && dst_h == 200) {
		const int lin_stride = SysMemPage.Get_Width() + SysMemPage.Get_Pitch();
		if (lin_stride > 0 && SysMemPage.Get_Buffer()) {
			C2P_Render_Logical_To_ST_Screen(
				(const uint8_t *)SysMemPage.Get_Buffer(),
				lin_stride,
				(uint8_t *)dst_gb->Get_Buffer(),
				0,
				C2P_ST_SCREEN_HEIGHT,
				1);
			title_drawn = true;
		}
	}
	if (!title_drawn && src_w == dst_w && src_h == dst_h) {
		SysMemPage.Blit(*video_page);
	} else if (!title_drawn) {
		SysMemPage.Scale(*video_page, 0, 0, 0, 0, src_w, src_h, dst_w, dst_h, FALSE, NULL);
	}
}

GraphicBufferClass *Read_PCX_File(char *name, char *palette, void *Buff, long Size)
{
	int i, j;
	int scan_pos;
	unsigned char *file_ptr;
	int width;
	int height;
	unsigned char *buffer;
	PCX_HEADER header;
	PG_RGB *pal;
	unsigned char pool[POOL_SIZE];
	GraphicBufferClass *pic;

	CCFileClass file_handle(name);

	if (!file_handle.Is_Available()) {
		return (NULL);
	}

	file_handle.Open(READ);
	file_handle.Read(&header, sizeof(PCX_HEADER));

	if (header.id != 10 && header.version != 5 && header.pixelsize != 8) {
		return NULL;
	}

	header.x = SwapLE16(header.x);
	header.y = SwapLE16(header.y);
	header.x_end = SwapLE16(header.x_end);
	header.y_end = SwapLE16(header.y_end);
	header.xres = SwapLE16(header.xres);
	header.yres = SwapLE16(header.yres);
	header.byte_per_line = SwapLE16(header.byte_per_line);
	header.palette_type = SwapLE16(header.palette_type);

	width = header.x_end - header.x + 1;
	height = header.y_end - header.y + 1;
	if (header.byte_per_line <= 0)
		header.byte_per_line = (short)width;

	{
		const unsigned char color_planes = (unsigned char)header.color_planes;
		const int bpl = (int)header.byte_per_line;
		if (color_planes == 1 && bpl > 0 && bpl < width)
			width = bpl;
	}

	if (Buff) {
		buffer = (unsigned char *)Buff;
		i = Size / width;
		height = MIN(i - 1, height);
		pic = new GraphicBufferClass(width, height, buffer, Size);
		if (!(pic && pic->Get_Buffer())) {
			return NULL;
		}
	} else {
		const int bpl = (header.byte_per_line > 0) ? (int)header.byte_per_line : width;
		const int row_stride = (width > bpl) ? width : bpl;
		const long alloc_bytes = (long)row_stride * (long)(height + 4);
		pic = new GraphicBufferClass(width, height, NULL, alloc_bytes);
		if (!(pic && pic->Get_Buffer())) {
			return NULL;
		}
		pic->Set_Linear_Row_Padding_Bytes(row_stride - width);
	}

	buffer = (unsigned char *)pic->Get_Buffer();
	file_ptr = pool;
	file_handle.Read(pool, POOL_SIZE);

#define GET_BYTE()                                                                                 \
	((file_ptr >= &pool[POOL_SIZE])                                                                \
			? ((void)file_handle.Read(pool, POOL_SIZE), file_ptr = pool, (unsigned char)*file_ptr++) \
			: (unsigned char)*file_ptr++)
	{
		int BufferSize;
		int index;
		unsigned char byte;
		unsigned int runcount;
		unsigned char runvalue;
		long row_stride = (long)pic->Get_Width() + (long)pic->Get_Pitch();

		BufferSize = (int)header.byte_per_line;
		for (j = 0; j < height; j++) {
			scan_pos = j * row_stride;
			index = 0;
			do {
				byte = GET_BYTE();
				if ((byte & 0xC0) == 0xC0) {
					runcount = byte & 0x3F;
					runvalue = GET_BYTE();
				} else {
					runcount = 1;
					runvalue = byte;
				}
				for (; runcount && index < BufferSize; runcount--, index++) {
					if (index < width)
						buffer[scan_pos + index] = runvalue;
				}
			} while (index < BufferSize);
		}
	}
#undef GET_BYTE

	if (palette) {
		file_handle.Seek(-768L, SEEK_END);
		file_handle.Read(palette, 768L);

		pal = (PG_RGB *)palette;
		for (i = 0; i < 256; i++) {
			pal->red >>= 2;
			pal->green >>= 2;
			pal->blue >>= 2;
			pal++;
		}
	}

	file_handle.Close();
	return pic;
}
