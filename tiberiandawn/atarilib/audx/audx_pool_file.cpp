/*
 * audx_pool_file.cpp — Read spans from pool%04x.bin sidecars.
 */

#include "audx_pool_file.h"

#include "audx.h"
#include "function.h"

int AUDX_Format_Pool_Name(uint16_t pool_id, char *out, size_t out_cap)
{
	if (!out || out_cap < 13u || pool_id == 0)
		return 0;
	if (snprintf(out, out_cap, "pool%04x.bin", (unsigned)pool_id) >= (int)out_cap)
		return 0;
	return 1;
}

int AUDX_Pool_Read(uint16_t pool_id, uint32_t begin, uint32_t size, void *dst)
{
	char name[16];
	CCFileClass file;

	if (!dst || size == 0 || pool_id == 0)
		return 0;
	if (!AUDX_Format_Pool_Name(pool_id, name, sizeof(name)))
		return 0;

	file.Set_Name(name);
	if (!file.Is_Available())
		return 0;
	if (!file.Open(READ))
		return 0;
	if (file.Seek((long)begin, SEEK_SET) != (long)begin) {
		file.Close();
		return 0;
	}
	if (file.Read(dst, (long)size) != (long)size) {
		file.Close();
		return 0;
	}
	file.Close();
	return 1;
}
