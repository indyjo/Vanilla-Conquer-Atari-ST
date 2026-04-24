/*
 * ST test harness: real Load_Uncompress implementation, isolated from JSHELL.CPP
 * so we don't pull in unrelated runtime symbols.
 */

#include "function.h"

long Load_Uncompress(FileClass &file, BuffType &uncomp_buff, BuffType &dest_buff, void *reserved_data)
{
	unsigned short size;
	unsigned short file_size;
	void *sptr = uncomp_buff.Get_Buffer();
	void *dptr = dest_buff.Get_Buffer();
	int opened = false;
	CompHeaderType header;
	unsigned char raw_header[sizeof(CompHeaderType)];

	if (!file.Is_Open()) {
		if (!file.Open()) {
			return 0;
		}
		opened = true;
	}

	file.Read(&file_size, sizeof(file_size));
#ifdef BIG_ENDIAN
	file_size = (unsigned short)((file_size >> 8) | (file_size << 8));
#endif
	size = file_size;

	file.Read(&header, sizeof(header));
#ifdef BIG_ENDIAN
	{
		const unsigned char *p = (const unsigned char *)&header;
		header.Size = (long)p[2] | ((long)p[3] << 8) | ((long)p[4] << 16) | ((long)p[5] << 24);
		header.Skip = (short)(p[6] | (p[7] << 8));
	}
#endif
	size = (unsigned short)(size - (unsigned short)sizeof(header));

	if (header.Skip) {
		size = (unsigned short)(size - (unsigned short)header.Skip);
		if (reserved_data) {
			file.Read(reserved_data, header.Skip);
		} else {
			file.Seek(header.Skip, SEEK_CUR);
		}
		header.Skip = 0;
	}

	if (uncomp_buff.Get_Buffer() == dest_buff.Get_Buffer()) {
		sptr = Add_Long_To_Pointer(sptr, uncomp_buff.Get_Size() - (size + sizeof(header)));
	}

	raw_header[0] = (unsigned char)header.Method;
	raw_header[1] = (unsigned char)header.pad;
	raw_header[2] = (unsigned char)((unsigned long)header.Size & 0xFF);
	raw_header[3] = (unsigned char)(((unsigned long)header.Size >> 8) & 0xFF);
	raw_header[4] = (unsigned char)(((unsigned long)header.Size >> 16) & 0xFF);
	raw_header[5] = (unsigned char)(((unsigned long)header.Size >> 24) & 0xFF);
	raw_header[6] = 0;
	raw_header[7] = 0;
	Mem_Copy(raw_header, sptr, sizeof(raw_header));
	file.Read(Add_Long_To_Pointer(sptr, sizeof(header)), size);

	size = (unsigned short)Uncompress_Data(sptr, dptr);

	if (opened) {
		file.Close();
	}
	return (long)size;
}
