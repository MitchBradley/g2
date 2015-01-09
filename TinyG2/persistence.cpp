/*
 * persistence.cpp - persistence functions
 * This file is part of the TinyG2 project
 *
 * Copyright (c) 2013 - 2014 Alden S. Hart Jr.
 *
 * This file ("the software") is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 as published by the
 * Free Software Foundation. You should have received a copy of the GNU General Public
 * License, version 2 along with the software.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, you may use this file as part of a software library without
 * restriction. Specifically, if other files instantiate templates or use macros or
 * inline functions from this file, or you compile this file and link it with  other
 * files to produce an executable, this file does not by itself cause the resulting
 * executable to be covered by the GNU General Public License. This exception does not
 * however invalidate any other reasons why the executable file might be covered by the
 * GNU General Public License.
 *
 * THE SOFTWARE IS DISTRIBUTED IN THE HOPE THAT IT WILL BE USEFUL, BUT WITHOUT ANY
 * WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT
 * SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "tinyg2.h"
#include "persistence.h"
#include "canonical_machine.h"
#include "report.h"

#ifdef __AVR
#include "xmega/xmega_eeprom.h"
#endif

/***********************************************************************************
 **** STRUCTURE ALLOCATIONS ********************************************************
 ***********************************************************************************/

nvmSingleton_t nvm;

/***********************************************************************************
 **** GENERIC STATIC FUNCTIONS AND VARIABLES ***************************************
 ***********************************************************************************/

#ifdef __ARM

stat_t prepare_persistence_file();
stat_t write_persistent_values();
stat_t check_persistence_file_crc();
uint8_t active_file_index();

// Leaving this in for now in case bugs come up; we can remove it when we're confident
// it's stable
#if 0
# define DEBUG_PRINT(...) printf(__VA_ARGS__);
#else
# define DEBUG_PRINT(...) do {} while (0)
#endif

// Analogous to ritorno(a), but for the FatFS return codes and with optional debug output
#define fs_ritorno(a, msg) if((status_code=a) != FR_OK) \
	{ DEBUG_PRINT("%s res: %i\n", msg, status_code); return(STAT_PERSISTENCE_ERROR); }

/*
 We cycle between three different files, indexed by a suffix. Each time we need to write
 new values, we copy data from the current file to a new file with NEXT_FILE_INDEX, then
 delete the current file once the write is complete. This ensures that at least one recent
 copy of the file will be preserved if power is lost in the middle of a write.
 */
#define PERSISTENCE_DIR "persist"
#define PERSISTENCE_FILENAME(num) PERSISTENCE_DIR"/persist"#num".bin"
#define PERSISTENCE_FILENAME_CNT 3
#define NEXT_FILE_INDEX (nvm.file_index+1) % PERSISTENCE_FILENAME_CNT
#define PREV_FILE_INDEX (nvm.file_index+PERSISTENCE_FILENAME_CNT-1) % PERSISTENCE_FILENAME_CNT
const char* filenames[PERSISTENCE_FILENAME_CNT] = {
	PERSISTENCE_FILENAME(0), PERSISTENCE_FILENAME(1), PERSISTENCE_FILENAME(2) };

#define CRC_LEN 4

#endif

/***********************************************************************************
 **** CODE *************************************************************************
 ***********************************************************************************/

void persistence_init()
{
#ifdef __AVR
	nvm.base_addr = NVM_BASE_ADDR;
	nvm.profile_base = 0;
#endif
#ifdef __ARM
	nvm.file_index = 0;
	nvm.last_write_systick = SysTickTimer_getValue();
	nvm.write_failures = 0;
#endif
	return;
}

/*
 * read_persistent_value()	- return value (as float) by index
 *
 *	It's the responsibility of the caller to make sure the index does not exceed range
 */

#ifdef __AVR
stat_t read_persistent_value(nvObj_t *nv)
{
	int8_t nvm_byte_array[NVM_VALUE_LEN];
	uint16_t nvm_address = nvm.profile_base + (nv->index * NVM_VALUE_LEN);
	(void)EEPROM_ReadBytes(nvm_address, nvm_byte_array, NVM_VALUE_LEN);
	memcpy(&nv->value, &nvm_byte_array, NVM_VALUE_LEN);
	return (STAT_OK);
}
#endif // __AVR

#ifdef __ARM
stat_t read_persistent_value(nvObj_t *nv)
{
	ritorno(prepare_persistence_file());
	DEBUG_PRINT("file opened for reading\n");
	fs_ritorno(f_lseek(&nvm.file, nv->index * NVM_VALUE_LEN), "f_lseek during read");
	UINT br;
	fs_ritorno(f_read(&nvm.file, &nvm.io_buffer, NVM_VALUE_LEN, &br), "read value");
	if (br != NVM_VALUE_LEN) {
		return (STAT_PERSISTENCE_ERROR);
	}
	memcpy(&nv->value, &nvm.io_buffer, NVM_VALUE_LEN);
	DEBUG_PRINT("value copied from address %i in file: %f\n", nv->index * NVM_VALUE_LEN, nv->value);
	return (STAT_OK);
}
#endif // __ARM

/*
 * write_persistent_value() - write to NVM by index, but only if the value has changed.
 *
 *  On AVR, the write is performed immediately. On ARM, the new value is cached, and
 *  later written from the write_persistent_values_callback (batching writes should
 *  extend the lifetime of the SD card).
 *
 *	It's the responsibility of the caller to make sure the index does not exceed range
 *	Note: Removed NAN and INF checks on floats - not needed
 */

#ifdef __AVR
stat_t write_persistent_value(nvObj_t *nv)
{
	if (cm.cycle_state != CYCLE_OFF) return(rpt_exception(STAT_FILE_NOT_OPEN, NULL));	// can't write when machine is moving
	nvm.tmp_value = nv->value;
	ritorno(read_persistent_value(nv));
	if ((isnan((double)nv->value)) || (isinf((double)nv->value)) || (fp_NE(nv->value, nvm.tmp_value))) {
		memcpy(&nvm.byte_array, &nvm.tmp_value, NVM_VALUE_LEN);
		nvm.address = nvm.profile_base + (nv->index * NVM_VALUE_LEN);
		(void)EEPROM_WriteBytes(nvm.address, nvm.byte_array, NVM_VALUE_LEN);
	}
	nv->value =nvm.tmp_value;		// always restore value
	return (STAT_OK);
}
#endif // __AVR

#ifdef __ARM
stat_t write_persistent_value(nvObj_t *nv)
{
	nvm.tmp_value = nv->value;
	if (read_persistent_value(nv) != STAT_OK ||
		(isnan((double)nv->value)) ||
		(isinf((double)nv->value)) ||
		(nv->value != nvm.tmp_value)) { // use a bitwise equality check rather than fp_EQ
										// since underlying value might not really be a float
			nvm.write_cache.insert(std::pair<index_t, float>(nv->index, nvm.tmp_value));
	}
	nv->value =nvm.tmp_value;		// always restore value
	return (STAT_OK);
}
#endif // __ARM

/*
 * write_persistent_values_callback()
 *
 *	On ARM, write cached values to a file. No-op on AVR.
 */

stat_t write_persistent_values_callback()
{
#ifdef __ARM
	// Check the disk status to ensure we catch CS pin changes.
	// FIXME: it would be much better to do this with an interrupt!
	f_polldisk();
	if (nvm.write_cache.size()) {
		if (SysTickTimer_getValue() - nvm.last_write_systick < MIN_WRITE_INTERVAL) return (STAT_NOOP);
		if(write_persistent_values() == STAT_OK) {
			nvm.write_cache.clear();
			nvm.write_failures = 0;
		} else {
			// if the write failed, make sure no half-written output file exists
			f_unlink(filenames[NEXT_FILE_INDEX]);
			if (++nvm.write_failures >= MAX_WRITE_FAILURES) {
				nvm.write_cache.clear(); // give up on these values
				nvm.write_failures = 0;  // but try again if we get more values later
			}
		}
		nvm.last_write_systick = SysTickTimer_getValue();
		return STAT_OK;
	}
	
#endif
	return STAT_NOOP;
}

#ifdef __ARM

/*
 * active_file_index()
 *
 * Determines which of the existing files is most current and should be used for
 *  value reads. If no files exist, return 0. This assumes that no more than two
 *  files exist at any one time, which should always be the case under our updating
 *  scheme (described in a comment near the PERSISTENCE_FILENAME define).
 */
uint8_t active_file_index()
{
	uint8_t i = 0;
	for (; i < PERSISTENCE_FILENAME_CNT; ++i) {
		if (f_stat(filenames[i], nullptr) == FR_OK) {
			uint8_t next = (i+1)%PERSISTENCE_FILENAME_CNT;
			if (next > i && f_stat(filenames[next], nullptr) == FR_OK) {
				i = next;
			}
			break;
		}
	}
	return i;
}

/*
 * prepare_persistence_file()
 *
 *	ARM only. Ensures that the file is open and has a valid CRC. This should be called
 *  prior to using the file in any other function.
 */
stat_t prepare_persistence_file()
{
	// if the file is already open and valid, no further prep is necessary.
	// NOTE: we don't close the file after every use because the higher latency
	// would slow down consecutive reads. However, we still need to re-validate before
	// every use to ensure that the card status hasn't changed.
	if (f_is_open(&nvm.file) && validate(&nvm.file) == FR_OK) return STAT_OK;
	
	// mount volume if necessary
	if (!nvm.fat_fs.fs_type) {
		fs_ritorno(f_mount(&nvm.fat_fs, "", 1), "mount");		/* Give a work area to the default drive */
	}
	f_mkdir(PERSISTENCE_DIR);
	uint8_t index = active_file_index();
	fs_ritorno(f_open(&nvm.file, filenames[index], FA_READ | FA_OPEN_EXISTING), "open input");
	nvm.file_index = index;
	
	// if CRC doesn't match, delete file and return error
	if (check_persistence_file_crc() != STAT_OK) {
		f_close(&nvm.file);
		f_unlink(filenames[nvm.file_index]);
		nvm.file_index = 0;
		return STAT_PERSISTENCE_ERROR;
	}
	// OK to delete old file now (if it still exists), since we know the current one is good
	f_unlink(filenames[PREV_FILE_INDEX]);
	return STAT_OK;
}

/*
 * check_persistence_file_crc()
 *
 *	ARM only. Helper function that checks the CRC of the persistence file. Assumes
 *   the file is already open.
 */
stat_t check_persistence_file_crc()
{
	uint32_t crc = 0;
	uint32_t filecrc = NAN;
	UINT br;
	fs_ritorno(f_lseek(&nvm.file, 0), "crc check seek");
	while (!f_eof(&nvm.file)) {
		fs_ritorno(f_read(&nvm.file, &nvm.io_buffer, IO_BUFFER_SIZE, &br), "file read during CRC check");
		if (f_eof(&nvm.file)) {
			br -= CRC_LEN; // don't include old CRC in current CRC calculation
			memcpy(&filecrc, nvm.io_buffer+br, CRC_LEN); // copy old CRC out of read buffer
		}
		// update calculated CRC
		crc = crc32(crc, nvm.io_buffer, br);
	}
	DEBUG_PRINT("crc: %lu from file, %lu calculated\n", filecrc, crc);
	return crc == filecrc ? STAT_OK : STAT_PERSISTENCE_ERROR;
}

/*
 * write_persistent_values()
 * 
 * ARM only. Writes all the values from the write cache to the SD card. Since we can't
 *  rewrite individual pieces of data in the middle of an existing file, this requires
 *  rewriting all the data into a new file.
 */
stat_t write_persistent_values()
{
	DEBUG_PRINT("writing new version\n");
	
	FIL f_out;
	UINT bw;
	
	if (cm.cycle_state != CYCLE_OFF) return(rpt_exception(STAT_FILE_NOT_OPEN, NULL));	   // can't write when machine is moving
	
	// attempt to open file with previously persisted values
	if (prepare_persistence_file() == STAT_OK) {
		fs_ritorno(f_lseek(&nvm.file, 0), "f_lseek to input file start");
	}
	
	// open new file for writing updated values
	fs_ritorno(f_open(&f_out, filenames[NEXT_FILE_INDEX], FA_WRITE | FA_OPEN_ALWAYS), "open output");
	fs_ritorno(f_sync(&f_out), "sync output file");
	DEBUG_PRINT("opened %s for writing\n", filenames[NEXT_FILE_INDEX]);
	
	uint32_t crc = 0;
	index_t step = IO_BUFFER_SIZE/NVM_VALUE_LEN;
	auto temp_write_cache = nvm.write_cache;	// don't modify the write cache until after success/failure
	for (index_t cnt = 0; (temp_write_cache.size()) || (f_remain(&nvm.file) > CRC_LEN); cnt += step) {
		
		// attempt to read the last persisted values from the older file into the buffer, padding with 0s
		// if no old values can be read
		index_t io_byte_count = f_is_open(&nvm.file) ? std::min(IO_BUFFER_SIZE, f_remain(&nvm.file)-CRC_LEN) : IO_BUFFER_SIZE;
		f_read(&nvm.file, &nvm.io_buffer, io_byte_count, &bw);
		memset(nvm.io_buffer+bw, 0, io_byte_count-bw);
		
		// update the values in the buffer from the write cache
		DEBUG_PRINT("read old block (cnt: %i, bytes_to_read: %i)\n", cnt, io_byte_count);
		for (auto i = temp_write_cache.lower_bound(cnt);
			 i != temp_write_cache.lower_bound(cnt+step);
			 i = temp_write_cache.erase(i)) { // map::erase returns an incremented iterator in C++11
			index_t index = (i->first - cnt) * NVM_VALUE_LEN;
			memcpy(nvm.io_buffer+index, &i->second, NVM_VALUE_LEN);
			DEBUG_PRINT("item index: %i, write index: %i (cnt: %i), value: %f\n", i->first, index, cnt, i->second);
		}
		
		// write updated values to output file and sync
		fs_ritorno(f_write(&f_out, &nvm.io_buffer, io_byte_count, &bw), "new file write");
		if (bw != io_byte_count) return (STAT_PERSISTENCE_ERROR);
		fs_ritorno(f_sync(&f_out), "out sync");
		
		// update CRC
		crc = crc32(crc, nvm.io_buffer, io_byte_count);
	}
	
	// write CRC in final 4 bytes
	fs_ritorno(f_write(&f_out, &crc, CRC_LEN, &bw), "write crc");
	DEBUG_PRINT("wrote crc: %lu\n", crc);

	// close both old and new files
	fs_ritorno(f_close(&f_out), "close output");
	if (f_is_open(&nvm.file)) {
		fs_ritorno(f_close(&nvm.file), "close input");
		// if we made it here, it's now safe to delete the older file
		fs_ritorno(f_unlink(filenames[nvm.file_index]), "old file delete");
		DEBUG_PRINT("deleted obsolete file %s\n", filenames[nvm.file_index]);
		nvm.file_index = 0;
	}
	
	return (STAT_OK);
}
#endif // __ARM





