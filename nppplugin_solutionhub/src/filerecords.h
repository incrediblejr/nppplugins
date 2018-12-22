#pragma once

struct FileRecordHeader {
	unsigned short recordsize;		// including header
	unsigned short filename_offset;
	unsigned short date_offset;
};

struct FileRecord {
	FileRecord(const wchar_t *p, const wchar_t *fn, const wchar_t *d)
		:path(p), filename(fn), date(d)	{ }

	const wchar_t *path, *filename, *date;
};

struct FileRecords {
	FileRecords() : base(0), datapart_base(0), num_records(0) {}
	FileRecords(const char *records) : base(records) {
		num_records = *((unsigned*)base);
		datapart_base = base + ((num_records+1)*sizeof(unsigned));
	}

	FileRecord filerecord(unsigned i)
	{
		const unsigned so_frh = sizeof(FileRecordHeader);

		unsigned offset = *((unsigned*)(base+ ( (i+1)*sizeof(unsigned) )));
		const char *frh = (datapart_base+offset);
		FileRecordHeader const &fr = *(FileRecordHeader*)(frh);
		const char *filedata_base = frh+so_frh;

		const wchar_t *p = (const wchar_t *)(filedata_base);
		const wchar_t *fn = (const wchar_t *)(filedata_base+fr.filename_offset);
		const wchar_t *d = (const wchar_t *)(filedata_base+fr.date_offset);

		return FileRecord(p, fn, d);
	}

	const char *base;
	const char *datapart_base;
	unsigned num_records;
};
