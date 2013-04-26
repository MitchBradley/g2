/*
 * config.h - configuration sub-system
 *
 * Copyright (c) 2010 - 2013 Alden S. Hart Jr.
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
/*
 * See config_data.h for a general description of the config system and its use.
 * Notes in this section concern the internals and programming.
 */
/**** cmdObj lists ****
 *
 * 	Commands and groups of commands are processed internally a doubly linked list
 *	cmdObj_t structures. This isolates the command and config internals from the 
 *	details of communications, parsing and display in text mode and JSON mode.
 *	The first element of the list is designated the response header element ("r") 
 *	but the list can also be serialized as a simple object by skipping over the header
 *
 *	To use the cmd list first reset it by calling cmd_reset_list(). This initializes
 *	the header, marks the the objects as TYPE_EMPTY, resets the shared string, and 
 *	terminates the last element by setting its NX pointer to NULL. When you use the 
 *	list you can terminate your own last element, or just leave the EMPTY elements 
 *	to be skipped over during outpout serialization.
 * 
 * 	We don't use recursion so parent/child nesting relationships are captured in a 
 *	'depth' variable, This must remain consistent if the curlies  are to work out. 
 *	In general you should not have to track depth explicitly if you use cmd_reset_list()
 *	or the accessor functions like cmd_add_integer() or cmd_add_message(). 
 *	If you see problems with curlies check the depth values in the lists.
 *
 *	Use the cmd_print_list() dispatcher for all JSON and text output. Do not simply 
 *	run through printf.
 */
/*	Cmd object string handling
 *
 *	It's very expensive to allocate sufficient string space to each cmdObj, so cmds 
 *	use a cheater's malloc. A single string of length CMD_SHARED_STRING_LEN is shared
 *	by all cmdObjs for all strings. The observation is that the total rendered output
 *	in JSON or text mode cannot exceed the size of the output buffer (typ 256 bytes),
 *	So some number less than that is sufficient for shared strings. This is all mediated 
 *	through cmd_copy_string() and cmd_copy_string_P(), and cmd_reset_list().
 */
/*	Other Notes:
 *
 *	CMD_BODY_LEN needs to allow for one parent JSON object and enough children
 *	to complete the largest possible operation - usually the status report.
 */
#ifndef _CONFIG_H_
#define _CONFIG_H_

/***** PLEASE NOTE:
# include "config_app.h"	// is present at the end of this file 
*/

#ifdef __cplusplus
extern "C"{
#endif 

/***********************************************************************************
 **** COMPILER SWITCHES ************************************************************
 ***********************************************************************************/
// Comment out what you don't need to skinny down the FLASH / RAM footprint

#define __ENABLE_TEXTMODE
#define __ENABLE_PERSISTENCE
#define __ENABLE_USART_DEVICE
#define __ENABLE_SPI_DEVICE
#define __ENABLE_PGM_FILE_DEVICE

/***********************************************************************************
 **** DEFINITIONS AND SETTINGS *******************************************************
 ***********************************************************************************/

// Sizing and footprints			// chose one based on # of elements in cmdArray
//typedef uint8_t index_t;			// use this if there are < 256 indexed objects
typedef uint16_t index_t;			// use this if there are > 255 indexed objects

									// defines allocated from stack (not-pre-allocated)
#define CMD_FORMAT_LEN 80			// print formatting string max length
#define CMD_MESSAGE_LEN 80			// sufficient space to contain end-user messages

									// pre-allocated defines (take RAM permanently)
#define CMD_SHARED_STRING_LEN 80	// shared string for string values
#define CMD_BODY_LEN 16				// body elements - allow for 1 parent + N children
									// (each body element takes 23 bytes of RAM)

// Stuff you probably don't want to change 

#define NO_MATCH (index_t)0xFFFF
#define CMD_GROUP_LEN 3				// max length of group prefix
#define CMD_TOKEN_LEN 5				// mnemonic token string: group prefix + short token
#define CMD_FOOTER_LEN 18			// sufficient space to contain a JSON footer array
#define CMD_LIST_LEN (CMD_BODY_LEN+2)// +2 allows for a header and a footer
#define CMD_MAX_OBJECTS (CMD_BODY_LEN-1)// maximum number of objects in a body string

#define CMD_STATUS_REPORT_LEN CMD_MAX_OBJECTS 	// max number of status report elements - see cfgArray
									// must also line up in cfgArray, se00 - seXX

#define NVM_VALUE_LEN 4				// NVM value length (float, fixed length)
#define NVM_BASE_ADDR 0x0000		// base address of usable NVM

enum objType {						// object / value typing for config and JSON
	TYPE_EMPTY = 0,					// object has no value (which is not the same as "NULL")
	TYPE_NULL,						// value is 'null' (meaning the JSON null value)
	TYPE_BOOL,						// value is "true" (1) or "false"(0)
	TYPE_INTEGER,					// value is a uint32_t
	TYPE_FLOAT,						// value is a floating point number
	TYPE_FLOAT_UNITS,				// value is a floating point number that may require units conversion for display
	TYPE_STRING,					// value is in string field
	TYPE_ARRAY,						// value is array element count, values are CSV ASCII in string field
	TYPE_PARENT						// object is a parent to a sub-object
};

enum tgCommunicationsMode {
	TEXT_MODE = 0,					// text command line mode
	JSON_MODE,						// strict JSON construction
	JSON_MODE_RELAXED				// relaxed JSON construction (future)
};

enum jsonFormats {					// json output print modes
	JSON_NO_PRINT = 0,				// don't print anything if you find yourself in JSON mode
	JSON_OBJECT_FORMAT,				// print just the body as a json object
	JSON_RESPONSE_FORMAT			// print the header/body/footer as a response object
};

enum textFormats {					// text output print modes
	TEXT_NO_PRINT = 0,				// don't print anything if you find yourself in TEXT mode
	TEXT_INLINE_PAIRS,				// print key:value pairs as comma separated pairs
	TEXT_INLINE_VALUES,				// print values as commas separated values
	TEXT_MULTILINE_FORMATTED		// print formatted values on separate lines with formatted print per line
};

enum textVerbosity {
	TV_SILENT = 0,					// no response is provided
	TV_PROMPT,						// returns prompt only and exception messages
	TV_MESSAGES,					// returns prompt only and exception messages
	TV_CONFIGS,						// returns prompt, messages and echo config commands. Gcode blocks are not echoed 
	TV_VERBOSE						// returns all prompts, messages, configs and gcode blocks
};

enum jsonVerbosity {
	JV_SILENT = 0,					// no response is provided for any command
	JV_FOOTER,						// responses contain  footer only; no command echo, gcode blocks or messages
	JV_CONFIGS,						// echo configs; gcode blocks are not echoed; messages are not echoed
	JV_MESSAGES,					// echo configs; gcode messages only (if present); no block echo or line numbers
	JV_LINENUM,						// echo configs; gcode blocks return messages and line numbers as present
	JV_VERBOSE						// echos all configs and gcode blocks, line numbers and messages
};

/**** operations flags and shorthand ****/

#define F_INITIALIZE	0x01			// initialize this item (run set during initialization)
#define F_PERSIST 		0x02			// persist this item when set is run
#define F_NOSTRIP		0x04			// do not strip the group prefix from the token
#define _f00			0x00
#define _fin			F_INITIALIZE
#define _fpe			F_PERSIST
#define _fip			(F_INITIALIZE | F_PERSIST)
#define _fns			F_NOSTRIP
#define _f07			(F_INITIALIZE | F_PERSIST | F_NOSTRIP)

/**** Structures ****/

typedef struct cmdString {				// shared string object
	uint8_t wp;							// current string array index for len < 256 bytes
//	uint16_t wp;						// use this value is string len > 255 bytes
	char_t string[CMD_SHARED_STRING_LEN];
} cmdStr_t;

typedef struct cmdObject {				// depending on use, not all elements may be populated
	struct cmdObject *pv;				// pointer to previous object or NULL if first object
	struct cmdObject *nx;				// pointer to next object or NULL if last object
	index_t index;						// index of tokenized name, or -1 if no token (optional)
	int8_t depth;						// depth of object in the tree. 0 is root (-1 is invalid)
	int8_t type;						// see cmdType
	int8_t precision;					// decimal precision for reporting (JSON)
	float value;						// numeric value
	char_t token[CMD_TOKEN_LEN+1];		// full mnemonic token for lookup
	char_t group[CMD_GROUP_LEN+1];		// group prefix or NUL if not in a group
	char_t (*stringp)[];				// pointer to array of characters from shared character array
} cmdObj_t; 							// OK, so it's not REALLY an object

typedef stat_t (*fptrCmd)(cmdObj_t *cmd);// required for cmd table access
typedef void (*fptrPrint)(cmdObj_t *cmd);// required for PROGMEM access

typedef struct cfgItem {
	char_t group[CMD_GROUP_LEN+1];		// group prefix (with NUL termination)
	char_t token[CMD_TOKEN_LEN+1];		// token - stripped of group prefix (w/NUL termination)
	uint8_t flags;						// operations flags - see defines below
 	int8_t precision;					// decimal precision for display (JSON)
 	const char_t *format;				// pointer to formatted print string in FLASH
	fptrPrint print;					// print binding: aka void (*print)(cmdObj_t *cmd);
	fptrCmd get;						// GET binding aka uint8_t (*get)(cmdObj_t *cmd)
	fptrCmd set;						// SET binding aka uint8_t (*set)(cmdObj_t *cmd)
	float *target;						// target for writing config value
	float def_value;					// default value for config item
} cfgItem_t;

/**** static allocation and definitions ****/

extern cmdStr_t cmdStr;
extern cmdObj_t cmd_list[];
extern const cfgItem_t cfgArray[];

#define cmd_header cmd_list
#define cmd_body  (cmd_list+1)

/**** Global scope function prototypes ****/

void cfg_init(void);

// main entry points for core access functions
stat_t cmd_get(cmdObj_t *cmd);			// main entry point for get value
stat_t cmd_set(cmdObj_t *cmd);			// main entry point for set value
void cmd_print(cmdObj_t *cmd);			// main entry point for set value
void cmd_persist(cmdObj_t *cmd);		// main entry point for persistence

// helpers
index_t cmd_get_index(const char_t *group, const char_t *token);
index_t	cmd_index_max (void);
uint8_t cmd_index_lt_max(index_t index);
uint8_t cmd_index_ge_max(index_t index);
uint8_t cmd_index_is_single(index_t index);
uint8_t cmd_index_is_group(index_t index);
uint8_t cmd_index_lt_groups(index_t index);
uint8_t cmd_group_is_prefixed(char_t *group);

// generic internal functions and accessors
stat_t set_nul(cmdObj_t *cmd);		// set nothing (no operation)
stat_t set_ui8(cmdObj_t *cmd);		// set uint8_t value
stat_t set_01(cmdObj_t *cmd);		// set a 0 or 1 value with validation
stat_t set_012(cmdObj_t *cmd);		// set a 0, 1 or 2 value with validation
stat_t set_int(cmdObj_t *cmd);		// set uint32_t integer value
stat_t set_flt(cmdObj_t *cmd);		// set floating point value
stat_t set_flu(cmdObj_t *cmd);		// set floating point value with unit conversion

stat_t get_nul(cmdObj_t *cmd);		// get null value type
stat_t get_ui8(cmdObj_t *cmd);		// get uint8_t value
stat_t get_int(cmdObj_t *cmd);		// get uint32_t integer value
stat_t get_flt(cmdObj_t *cmd);		// get floating point value
stat_t get_flu(cmdObj_t *cmd);		// get floating point value with unit conversion

void print_nul(cmdObj_t *cmd);		// print nothing (no operation)
void print_str(cmdObj_t *cmd);		// print a string value
void print_ui8(cmdObj_t *cmd);		// print unit8_t value
void print_int(cmdObj_t *cmd);		// print uint32_t integer value
void print_flt(cmdObj_t *cmd);		// print floating point value
void print_lin(cmdObj_t *cmd);		// print floating point linear value w/unit conversion
void print_rot(cmdObj_t *cmd);		// print floating point rotary value

#define get_format(index) (const char *)cfgArray[index].format

stat_t set_grp(cmdObj_t *cmd);		// set data for a group
stat_t get_grp(cmdObj_t *cmd);		// get data for a group

// object and list functions
void cmd_get_cmdObj(cmdObj_t *cmd);
cmdObj_t *cmd_reset_obj(cmdObj_t *cmd);
cmdObj_t *cmd_reset_list(void);
stat_t cmd_copy_string(cmdObj_t *cmd, const char_t *src);
cmdObj_t *cmd_add_object(const char_t *token);
cmdObj_t *cmd_add_integer(const char_t *token, const uint32_t value);
cmdObj_t *cmd_add_float(const char_t *token, const float value);
cmdObj_t *cmd_add_string(const char_t *token, const uint8_t *string);
cmdObj_t *cmd_add_message(const char_t *string);
void cmd_print_list(stat_t status, uint8_t text_flags, uint8_t json_flags);

/*********************************************************************************************
 **** PLEASE NOTICE THAT CONFIG_APP.H IS HERE ************************************************
 *********************************************************************************************/
#include "config_app.h"

/*** Unit tests ***/

/* unit test setup */
//#define __UNIT_TEST_CONFIG			// uncomment to enable config unit tests
#ifdef __UNIT_TEST_CONFIG
void cfg_unit_tests(void);
#define	CONFIG_UNITS cfg_unit_tests();
#else
#define	CONFIG_UNITS
#endif // __UNIT_TEST_CONFIG

#ifdef __cplusplus
}
#endif

#endif // _CONFIG_H_
