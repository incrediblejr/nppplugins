#include "file_repository_common.h"
#include "filerecords.h"
#include <Windows.h>

#include "string/string_utils.h"
#include "stream.h"
#include "debug.h"

// dummy
struct dummy_timer {
	double interval() { return 0;  }
	void start() {}
	void stop() {}
};

#include <assert.h>
namespace {
	void debug_print_db(std::vector<char> const &db) {
		using namespace npp;

		const char *b = &db[0];
		unsigned num_records = stream::unpack<unsigned>(b);

		for(unsigned i=0; i<num_records; ++i) {
			const RecordHeader &rh =  *(const RecordHeader*)b;
			const wchar_t *start = (const wchar_t *)(b+sizeof(RecordHeader));

			DEBUG_PRINT("[DB %d] fn(%S)", i, start);
			stream::advance(b, rh.record_size);
		}
	}

	inline unsigned date_length_bytes(const RecordHeader &rh)
	{
		const unsigned sow = sizeof(wchar_t);
		const unsigned date_len = (rh.record_size-(rh.filename_length+rh.filename_offset)*sow-sizeof(rh));

		return date_len;
	}

	inline unsigned date_length(const RecordHeader &rh)
	{
		const unsigned sow = sizeof(wchar_t);
		return (date_length_bytes(rh) / sow);
	}
}

namespace filerepo {

	void make_internal_datestring_st(wchar_t *res, const void *stin)
	{
		SYSTEMTIME &sys_local = *((SYSTEMTIME*)stin);
		const wchar_t *date_format = L"%02d/%02d/%04d %02d:%02d";

		unsigned datestring_len = 16+1;

		swprintf(res, datestring_len, date_format, sys_local.wDay, sys_local.wMonth, sys_local.wYear, sys_local.wHour, sys_local.wMinute);
	}

	void make_internal_datestring_ft(wchar_t *res, const void *ftin)
	{
		SYSTEMTIME sys_utc, sys_local;

		const FILETIME *ft = (const FILETIME *)ftin;

		FileTimeToSystemTime(ft, &sys_utc);
		SystemTimeToTzSpecificLocalTime(0, &sys_utc, &sys_local);

		make_internal_datestring_st(res, &sys_local);
	}

	namespace aux {
		RecordHeader make_recordheader(const wchar_t *fullname, unsigned datestring_len)
		{
			const unsigned sow = sizeof(wchar_t);

			unsigned full_len = (unsigned)wcslen(fullname);
			unsigned path_len = file_util::pathlength(fullname);

			unsigned filename_len = full_len-path_len;

			// NEW : +2 for 2 null terminates(file and date)
			unsigned num_null = (datestring_len == 0 ? 1 : 2);

			unsigned record_size = sizeof(RecordHeader);
			record_size += sow*(full_len+datestring_len+num_null);

			RecordHeader header;
			header.record_size = (unsigned short)record_size;
			header.filename_length = (unsigned char)(filename_len+1);	//! null termination
			header.filename_offset = (unsigned char)path_len;		//! *NO* null termination

			return header;
		}

		void pack_changeheader(std::vector<char> &s, unsigned changetype, const wchar_t *fullname, unsigned datestring_len, const wchar_t *datestring)
		{
			using namespace npp;
			const unsigned sow = sizeof(wchar_t);

			RecordHeader recordheader = make_recordheader(fullname, datestring_len);

			stream::pack(s, changetype);
			unsigned total_db_size = recordheader.record_size+sizeof(unsigned); // record + header

			stream::pack(s, total_db_size);
			unsigned num_records = 1;

			stream::pack(s, num_records);
			stream::pack(s, recordheader);

			const unsigned datestring_bytelen = date_length_bytes(recordheader);
			// NOTE : wchar_t size accounted for!
			unsigned filename_size = (recordheader.record_size-sizeof(RecordHeader))-(datestring_bytelen);

			stream::pack_bytes(s, fullname, filename_size-sow); //! Note
			stream::pack(s, (wchar_t)0);
			if(datestring_len) {
				stream::pack_bytes(s, datestring, datestring_bytelen-sow); //! Note
				stream::pack(s, (wchar_t)0);
			}
		}

		void insert_filerecord(std::vector<char> &db, const wchar_t *filename, const wchar_t *date)
		{
			using namespace npp;
			const unsigned sow = sizeof(wchar_t);

			//////////////////////////////////////////////////////////////////////////
			RecordHeader recordheader = make_recordheader(filename, 16);

			const unsigned datestring_bytelen = 17*sow; //! NOTE : *17* include null

			const char *b = &db[0];
			const unsigned count = stream::unpack<unsigned>(b);
			//unsigned filename_size = (recordheader.record_size-sizeof(RecordHeader))-(datestring_bytelen);

			stream::make_room(db, recordheader.record_size);
			(*((unsigned*)&db[0]))++; // INC

			b = &db[0]+sizeof(unsigned);
			const char *startdb = b;

			const unsigned dbsize = (unsigned)db.size()-sizeof(unsigned);

			const wchar_t *compfilename = filename+(recordheader.filename_offset);

			const char *destination = 0;
			for(unsigned i=0;i<count;++i) {
				const RecordHeader &rh = *(const RecordHeader*)b;
				const wchar_t *current_filename = (const wchar_t *)(b+sizeof(RecordHeader));
				current_filename += rh.filename_offset;
				int res = _wcsicmp(current_filename, compfilename);

				if(res > 0) {
					destination = b;
					break;
				}

				stream::advance(b, rh.record_size);
			}

			if(destination) {
				unsigned shift_bytes = (unsigned)(destination-startdb);
				unsigned num_bytes_to_copy = (dbsize - recordheader.record_size);
				num_bytes_to_copy -= shift_bytes;

				void *dest = (void*)(destination+recordheader.record_size);
				memmove(dest, destination, num_bytes_to_copy);
				b = destination;
			}

			const unsigned char fn_offset = recordheader.filename_offset;
			const unsigned char fn_length = recordheader.filename_length;

			// last
			assert(sizeof(void*) == sizeof(char*));
			wchar_t null(0);
			char *dest = (char*)b;
			memcpy(dest, &recordheader, sizeof(recordheader)); dest+=sizeof(recordheader);

			const unsigned path_len = fn_offset; // includes 0-term

			//! Path
			unsigned nb = path_len*sow; // do not copy (non-existing) 0-term
			memcpy(dest, filename, nb); dest+=nb;

			//! Filename
			const wchar_t *fn = filename+(path_len); // path_len includes the 0-term
			nb = (fn_length*sow)-sow;
			memcpy(dest, fn, nb); dest+=nb;
			memcpy(dest, &null, sow); dest+=sow;

			//! Date
			nb = datestring_bytelen-sow;
			memcpy(dest, date, nb); dest+=nb;
			memcpy(dest, &null, sow); dest+=sow;

			assert((unsigned)(dest-b) == recordheader.record_size);
		}

		void merge_dbs(std::vector<char> &db, const char *db2, unsigned db2_size)
		{
			// NO CHANGE NEEDED
			using namespace npp;
			unsigned dbsize = (unsigned)db.size()-sizeof(unsigned); //! BEFORE

			stream::make_room(db, db2_size-sizeof(unsigned));

			const char *b = &db[0];

			unsigned db_num_records = npp::stream::unpack<unsigned>(b);
			const char *startdb = b;

			const char *c = db2;
			unsigned db2_num_records = *(unsigned*)c; c += sizeof(unsigned);

			const unsigned total_records = db_num_records+db2_num_records; // when all is said and done
			*((unsigned*)&db[0]) = total_records; // set

			unsigned i = 0, j = 0;
			while(i<db_num_records && j<db2_num_records) {
				const RecordHeader &rh = *(const RecordHeader*)b;
				const wchar_t *filename = (const wchar_t *)(b+sizeof(RecordHeader));
				filename += rh.filename_offset;

				const RecordHeader &rhc = *(const RecordHeader*)c;
				const wchar_t *innerfilename = (const wchar_t *)(c+sizeof(RecordHeader));
				innerfilename += rhc.filename_offset;

				int res = _wcsicmp(filename, innerfilename);
				if(res > 0) {
					// insert string to into DB
					unsigned insert_record_size = rhc.record_size;

					// first shift all currently present
					void *destination = (void*)(b+insert_record_size);
					const void *source = b;
					unsigned num_bytes = dbsize - (unsigned)(b-startdb);
					memmove(destination, source, num_bytes);
#ifdef _DEBUG
					const char *endpoint = ((char*)(destination))+num_bytes;

					unsigned check = (unsigned)(endpoint - &db[0]);
					if(check > db.size()) {
						unsigned overwrite_bytes = check - (unsigned)db.size();
						overwrite_bytes += 0;
						assert(!"!OVERWRITE!");
					}
#endif
					// copy in record
					destination = (void*)b;
					source = c;
					memcpy(destination, source, insert_record_size);

					// bookkeeping
					db_num_records = db_num_records+1;
					dbsize = dbsize+insert_record_size; // store new size

					++j; // advance index of db2
					c += insert_record_size;

				} else {
					// continue advancing DB
					stream::advance(b, rh.record_size);
					++i;
				}
			}

			if(j != db2_num_records) {
				// copy what is left in db2
				//unsigned db2size = db2_size; // header included!
				unsigned size_left = db2_size - (unsigned)(c-db2);
				void *destination = (void*)b;
				const void *source = (const void*)c;
				memcpy(destination, source, size_left);
			}
		}

		void exclude_db(std::vector<char> &db, const char *db2, unsigned /*db2_size*/)
		{
			//! CHECKED
			const unsigned so_recordheader = sizeof(RecordHeader);

			using namespace npp;
			unsigned dbsize = (unsigned)db.size()-sizeof(unsigned); //! BEFORE

			unsigned removed_bytes = 0, removed_records = 0;

			const char *b = &db[0];

			unsigned db_num_records = npp::stream::unpack<unsigned>(b);
			const char *startdb = b;

			const char *c = db2;
			unsigned db2_num_records = *(unsigned*)c; c += sizeof(unsigned);

			unsigned i = 0, j = 0;
			while(i<db_num_records && j<db2_num_records) {
				const RecordHeader &rh = *(const RecordHeader*)b;
				const wchar_t *filename = (const wchar_t *)(b+so_recordheader);
				const unsigned current_record_size = rh.record_size;

				const RecordHeader &rhc = *(const RecordHeader*)c;
				const wchar_t *exclude_filename = (const wchar_t *)(c+so_recordheader);
				const bool remove = (0 == ::_wcsicmp(filename, exclude_filename));

				if(remove) {
					void *destination = (void*)b;
					const void *source = (const void*)(b+current_record_size);
					unsigned num_bytes = dbsize- (unsigned)(b-startdb)-current_record_size;
					memmove(destination, source, num_bytes);

					// advance db2
					++j;
					c += rhc.record_size;

					// current db bookkeeping
					removed_records = removed_records+1;
					removed_bytes = removed_bytes + current_record_size;
				} else {
					// continue advancing DB
					stream::advance(b, rh.record_size);
					++i;
				}

			}

			if(removed_bytes) {
				unsigned &num_records = *((unsigned*)&db[0]);
				num_records = num_records - removed_records;

				unsigned new_size = (unsigned)db.size()-removed_bytes;
				db.resize(new_size);
			}
		}

		void add_replace_db(std::vector<char> &db, const char *db2, unsigned db2_size)
		{
			const unsigned so_recordheader = sizeof(RecordHeader);

			using namespace npp;
			//unsigned dbsize = db.size()-sizeof(unsigned); //! BEFORE

			//unsigned removed_bytes = 0, removed_records = 0;

			const char *b = &db[0];

			unsigned db_num_records = npp::stream::unpack<unsigned>(b);

			const char *c = db2;
			unsigned db2_num_records = *(unsigned*)c; c += sizeof(unsigned);

			unsigned i = 0, j = 0;
			while(i<db_num_records && j<db2_num_records) {
				const RecordHeader &rh = *(const RecordHeader*)b;
				const wchar_t *filename = (const wchar_t *)(b+so_recordheader);

				const RecordHeader &rhc = *(const RecordHeader*)c;
				const wchar_t *exclude_filename = (const wchar_t *)(c+so_recordheader);

				const unsigned current_record_size = rh.record_size;
				const unsigned replace_record_size = rhc.record_size;
				if(current_record_size == replace_record_size) {
					bool replace = (0 == ::_wcsicmp(filename, exclude_filename));
					if(replace) {
						memcpy((void*)b, c, current_record_size);

						++j;
						c += rhc.record_size;
					}
				}

				stream::advance(b, rh.record_size);
				++i;

			}

			if(j != db2_num_records) {
				// copy what is left in db2
				//unsigned db2size = db2_size; // header included!
				unsigned size_left = db2_size - (unsigned)(c-db2);

				unsigned offset = (unsigned)(b - &db[0]);
				stream::make_room(db, size_left);

				void *destination = (&db[0]+offset);
				const void *source = (const void*)c;
				memcpy(destination, source, size_left);

				unsigned &num_records = *((unsigned*)&db[0]);
				num_records = num_records +(db2_num_records-j);
			}
		}

		unsigned search_db(std::vector<char> &result,
							const std::vector<char> &db,
							unsigned search_all,
							unsigned char num_include,
							unsigned char num_exclude,
							const wchar_t *include,
							const wchar_t *exclude,
							std::vector<char> &delayed_events)
		{
			using namespace npp;

			dummy_timer profiler;

			unsigned sow = sizeof(wchar_t);
			unsigned filerecord_header_size = sizeof(RecordHeader);

			unsigned full_size = (unsigned)db.size();
			unsigned num_records_in_db = *((unsigned*)&db[0]);

			unsigned datapart_size = full_size-(num_records_in_db*filerecord_header_size)-sizeof(unsigned);

			// Resulting vector :
			//	(1) num records
			//	(2) num records*unsigned (for offset forward to 'i's record)
			//	(3) num records*(FileRecord+data)
			//
			// Notes :
			//	* One more null term in FileRecord (as path is also null termed)
			//

			unsigned max_size_off_return =	datapart_size
											+(num_records_in_db*sow) // null term of path
											+(((num_records_in_db+1)*sizeof(unsigned))) // offsets + first header
											+(num_records_in_db*sizeof(FileRecordHeader))
											;

			result.resize(max_size_off_return);

			unsigned result_num_records = 0;
			unsigned result_datasize = 0;
			unsigned result_base_data_offset = (((num_records_in_db+1)*sizeof(unsigned)));

			// setup delayed information
			const unsigned delayed_buffersize = (unsigned)delayed_events.size()-sizeof(unsigned);

			const char *d = &delayed_events[0];
			unsigned num_delayed = stream::unpack<unsigned>(d);
			const char *delayed_start = d; // 'cache'/save
			unsigned delayed_consumed_bytes = 0;
			// end
			bool include_all_records = (0 == (num_include+num_exclude));

			const char *b = &db[0];
			const unsigned num_records = stream::unpack<unsigned>(b);

			DEBUG_PRINT("[Search2] Searching, num db records : %d\n", num_records);

			profiler.start();

			for(unsigned i=0; i<num_records; ++i) {
				const RecordHeader &rh =  *(const RecordHeader*)b;

				const wchar_t *start = (const wchar_t *)(b+sizeof(RecordHeader));
				bool bail = false;

				if(!include_all_records) {
					const wchar_t *searchstring = (search_all ? start : start+rh.filename_offset);
					unsigned char counter = num_exclude;
					const wchar_t *token = exclude;

					while(counter && !bail) {

						if(string_util::wstristr(searchstring, token)) {
							bail = true;
						}
						token += wcslen(token)+1;
						--counter;
					}

					if(!bail) {
						counter = num_include;
						token = include;
						const wchar_t *s = searchstring;
						while(counter && !bail) {

							const unsigned token_len = (unsigned)wcslen(token);

							if((s = string_util::wstristr(s, token)) == 0) {
								bail = true;
							} else {
								s = s+token_len;
							}

							token += token_len+1;
							--counter;
						}
					}
				}

				if(!bail) {
					//std::wstring full_extracted(start, rh.date_offset);
					if(num_delayed) {
						unsigned current_record_size = rh.record_size;

						const RecordHeader &rhd = *(const RecordHeader*)d;
						unsigned update_record_size = rhd.record_size;

						if(update_record_size == current_record_size) {
							const wchar_t *update_filename = (const wchar_t *)(d+sizeof(RecordHeader));
							bool update = (0 == ::_wcsicmp(start, update_filename));
							if(update) {
								memcpy((void*)b, d, current_record_size);

								// remove delayed
								void *destination = (void*)d;
								const void *source = (const void*)(d+current_record_size);
								unsigned num_bytes = delayed_buffersize- (unsigned)(d-delayed_start)-current_record_size;
								// can result in zero bytes
								memmove(destination, source, num_bytes);
								//
								--num_delayed;
								delayed_consumed_bytes += current_record_size;
							}
						}
					}

					const unsigned so_unsigned = sizeof(unsigned);
					const unsigned so_filerecord = sizeof(FileRecordHeader);

					unsigned filename_len = rh.filename_length;	// null included
					unsigned path_len = rh.filename_offset+1; // add null
					unsigned date_len = (rh.record_size-(rh.filename_length+rh.filename_offset)*sow-sizeof(rh)) / sow;

					const char *result_start = &result[0];
					const char *data_start = result_start+result_base_data_offset;

					unsigned filerecord_size =	sow*(filename_len+path_len+date_len)+so_filerecord;

					result_start += (so_unsigned * (result_num_records+1));
					*(unsigned*)(result_start) = result_datasize;

					FileRecordHeader fr;
					fr.recordsize = (unsigned short)filerecord_size;
					fr.filename_offset = (unsigned short)(path_len*sow);
					fr.date_offset = (unsigned short)(filerecord_size - (date_len*sow) - so_filerecord);

					wchar_t null(0);

					const char *data_destination = data_start+result_datasize;
					memcpy((void*)data_destination, &fr, sizeof(fr));
					data_destination+=sizeof(fr);
					//! Path
					memcpy((void*)data_destination, start, (path_len-1)*sow);
					data_destination += (path_len-1)*sow;
					memcpy((void*)data_destination, &null, sow);
					data_destination += sow;
					//! Filename and Date
					memcpy((void*)data_destination, (start+rh.filename_offset), (filename_len+date_len)*sow);

					// bookkeeping
					result_datasize = result_datasize + filerecord_size;
					result_num_records = result_num_records + 1;
				}

				stream::advance(b, rh.record_size);
			}

			profiler.stop();

			double search_time = profiler.interval();

			if(delayed_consumed_bytes) {
				*((unsigned*)&delayed_events[0]) = num_delayed;

				unsigned new_size = (unsigned)delayed_events.size()-delayed_consumed_bytes;
				delayed_events.resize(new_size);
			}

			DEBUG_PRINT("[Search2] Search time : %f milliseconds, num found records %d\n", search_time, result_num_records);

			*((unsigned*)&result[0]) = result_num_records;

			unsigned diff = num_records_in_db - result_num_records;
			if(diff) {
				unsigned new_base = (result_num_records+1)*sizeof(unsigned);
				//unsigned shift = result_base_data_offset - new_base;

				const char *start = &result[0];

				void *destination = (void*)(start+new_base);
				const void *source = (const void*)(start+result_base_data_offset);
				unsigned num_bytes = result_datasize;
				// can result in zero bytes
				memmove(destination, source, num_bytes);
			}

			const unsigned result_size = ((result_num_records+1)*sizeof(unsigned))+result_datasize;
			return result_size;
		}

		void rename_directory(std::vector<char> &db, const wchar_t *from, const wchar_t *to, std::vector<char> &/*temp_buffer*/)
		{
			using namespace npp;

			const unsigned so_unsigned = sizeof(unsigned);
			const unsigned sow = sizeof(wchar_t);

			unsigned current_dbsize = (unsigned)db.size()-sizeof(unsigned); // DB part(i.e. no header)
			unsigned num_records_in_db = *((unsigned*)&db[0]);

			const unsigned strlen_from = (unsigned)wcslen(from);
			const unsigned strlen_to = (unsigned)wcslen(to);

			const bool inplace = strlen_from == strlen_to;
			const bool allocate = strlen_to > strlen_from;
			const bool deallocate = strlen_from > strlen_to;

			unsigned diff_len_bytes = 0;
			unsigned diff_len = 0;
			if(allocate) {
				// need to inc storage
				diff_len = strlen_to-strlen_from;
				diff_len_bytes = diff_len*sow;
			} else if(deallocate) {
				diff_len = strlen_from-strlen_to;
				diff_len_bytes = diff_len*sow;
			}

			const char *b = &db[0];
			b += so_unsigned; // skip header

			unsigned current_offset = 0;
			unsigned deallocated_bytes = 0;

			DEBUG_PRINT("Rename start: current_size(%d)", current_dbsize);

			for(unsigned i=0; i<num_records_in_db; ++i) {
				RecordHeader &rh =  *(RecordHeader*)b;
				unsigned record_size = rh.record_size; // can change if we allocate

				const wchar_t *start = (const wchar_t *)(b+sizeof(RecordHeader));

				if(wcsstr(start, from)) {
					if(inplace) {
						memcpy((void*)start, (const char*)to, strlen_to*sizeof(wchar_t));
					} else if(deallocate) {
						memcpy((void*)start, (const char*)to, strlen_to*sizeof(wchar_t));

						char *destination = (char*)(b+sizeof(RecordHeader))+strlen_to*sizeof(wchar_t);
						const char *source = destination+diff_len_bytes;
						const unsigned shift_bytes = current_dbsize - (unsigned)(destination-(&db[0]+so_unsigned));	// destination does not have header

						memmove(destination, source, shift_bytes);

						rh.record_size -= (unsigned short)diff_len_bytes;
						rh.filename_offset -= (unsigned char)diff_len;

						//! Bookkeeping
						current_dbsize -= diff_len_bytes;
						deallocated_bytes += diff_len_bytes;

						record_size = rh.record_size;
					} else if(allocate) {
						//! Make room
						db.resize(db.size()+diff_len_bytes);
						current_dbsize = (unsigned)db.size()-so_unsigned; // remove header part

						//! Fetch once again
						b = (&db[0]+current_offset)+so_unsigned; // fetch b again and add header back!!

						RecordHeader &record_header = *(RecordHeader*)b;
						const wchar_t *record_start = (const wchar_t *)(b+sizeof(RecordHeader));

						//! Move first
						const char *source = (const char*)(record_start+strlen_from);
						char *destination = (char*)(source+diff_len_bytes);

						const unsigned shift_bytes = current_dbsize - (unsigned)(destination-(&db[0]+so_unsigned));	// destination does not have header
						memmove(destination, source, shift_bytes);

						//! All moved, copy in replacement
						memcpy((void*)record_start, (const char*)to, strlen_to*sizeof(wchar_t));

						//! Update record data
						record_header.record_size += (unsigned short)diff_len_bytes;
						record_header.filename_offset += (unsigned char)diff_len;

						record_size = record_header.record_size;
					}
				}

				current_offset += record_size;
				stream::advance(b, record_size);
			}

			db.resize(db.size()-deallocated_bytes);

			DEBUG_PRINT("Rename end: current_size(%d)", db.size());
		}
	}
}
