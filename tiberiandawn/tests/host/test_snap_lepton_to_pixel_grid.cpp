/*
 * Host identity test: LUT Snap_Lepton_To_Pixel_Grid vs the former mul+25-entry form.
 */

#include <cstdio>

enum {
	ICON_PIXEL_W = 24,
	ICON_LEPTON_W = 256
};

static int Snap_Lepton_To_Pixel_Grid_Old(int lepton)
{
	static unsigned short const snap_table[25] = {
		0, 11, 21, 32, 43, 53, 64, 75, 85, 96, 107, 117, 128,
		139, 149, 160, 171, 181, 192, 203, 213, 224, 235, 245, 256
	};

	unsigned short const value = (unsigned short)lepton;
	int const base = (short)(value & 0xFF00u);
	unsigned const fraction = value & 0x00FFu;
	unsigned const pixel = ((fraction * ICON_PIXEL_W) + (ICON_LEPTON_W / 2)) >> 8;

	return base + (int)snap_table[pixel];
}

static unsigned short const snap_table_new[256] = {
	  0,   0,   0,   0,   0,   0,  11,  11,  11,  11,  11,  11,  11,  11,  11,  11,
	 21,  21,  21,  21,  21,  21,  21,  21,  21,  21,  21,  32,  32,  32,  32,  32,
	 32,  32,  32,  32,  32,  32,  43,  43,  43,  43,  43,  43,  43,  43,  43,  43,
	 53,  53,  53,  53,  53,  53,  53,  53,  53,  53,  53,  64,  64,  64,  64,  64,
	 64,  64,  64,  64,  64,  64,  75,  75,  75,  75,  75,  75,  75,  75,  75,  75,
	 85,  85,  85,  85,  85,  85,  85,  85,  85,  85,  85,  96,  96,  96,  96,  96,
	 96,  96,  96,  96,  96,  96, 107, 107, 107, 107, 107, 107, 107, 107, 107, 107,
	117, 117, 117, 117, 117, 117, 117, 117, 117, 117, 117, 128, 128, 128, 128, 128,
	128, 128, 128, 128, 128, 128, 139, 139, 139, 139, 139, 139, 139, 139, 139, 139,
	149, 149, 149, 149, 149, 149, 149, 149, 149, 149, 149, 160, 160, 160, 160, 160,
	160, 160, 160, 160, 160, 160, 171, 171, 171, 171, 171, 171, 171, 171, 171, 171,
	181, 181, 181, 181, 181, 181, 181, 181, 181, 181, 181, 192, 192, 192, 192, 192,
	192, 192, 192, 192, 192, 192, 203, 203, 203, 203, 203, 203, 203, 203, 203, 203,
	213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 213, 224, 224, 224, 224, 224,
	224, 224, 224, 224, 224, 224, 235, 235, 235, 235, 235, 235, 235, 235, 235, 235,
	245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 245, 256, 256, 256, 256, 256,
};

static int Snap_Lepton_To_Pixel_Grid_New(int lepton)
{
	unsigned short const value = (unsigned short)lepton;
	int const base = (short)(value & 0xFF00u);
	unsigned const fraction = value & 0x00FFu;

	return base + (int)snap_table_new[fraction];
}

int test_snap_lepton_to_pixel_grid(void)
{
	unsigned long checked = 0;

	/* Every distinct input the game path can see: (unsigned short)lepton. */
	for (unsigned v = 0; v < 65536u; ++v) {
		int const lepton = (int)(short)v; /* also covers negative leptons via high bit */
		int const old_v = Snap_Lepton_To_Pixel_Grid_Old(lepton);
		int const new_v = Snap_Lepton_To_Pixel_Grid_New(lepton);
		if (old_v != new_v) {
			std::printf("FAIL lepton=%d (u16=%u): old=%d new=%d\n", lepton, v, old_v, new_v);
			return 1;
		}
		++checked;

		/* High bits above 16 must not matter (same cast as production). */
		int const wide = lepton | (int)0x5A5A0000;
		if (Snap_Lepton_To_Pixel_Grid_Old(wide) != Snap_Lepton_To_Pixel_Grid_New(wide)) {
			std::printf("FAIL wide lepton=%d: mismatch\n", wide);
			return 1;
		}
		++checked;
	}

	std::printf("PASS: Snap_Lepton_To_Pixel_Grid LUT identical to mul form (%lu checks)\n", checked);
	return 0;
}
