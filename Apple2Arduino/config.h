/* DAN][ configuration */

#define USE_ETHERNET     // enable Ethernet support
#define USE_FTP          // enable FTP support
#define USE_FAT_DISK     // enable FAT support
#define USE_RAW_DISK     // enable raw disk support

#undef USE_MEM_CHECK     // enable this for memory overflow checks
#undef DEBUG_SERIAL      // enable this for serial debug output
//#define DEBUG_BYTES  8   // only for debugging: enable capturing some debug bytes

/* Select boot program:
  1=no boot program
  2=original/stock v2 bootprogram by DL Marks
  3=bootpg with improved GUI (512byte)
  4=bootpg with cursor key controls (1024byte)
  5=bootpg with IP configuration option (1536byte)
  6=bootpg with flexible volume/SD card mapping (byte)
*/
#define BOOTPG 6

// default IPv4 address (4 bytes, comma separated)
#define FTP_IP_ADDRESS 0,0,0,0 // disabled by default (needs to be enabled manually through the configuration utility)

// default MAC address (6 bytes, comma separated)
#define FTP_MAC_ADDRESS 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED

// FTP ports: command port should normally be 21
#define FTP_CMD_PORT     21
// FTP data port can be any port
#define FTP_DATA_PORT    20

// Maximum number of VOLxx.PO files supported by FTP (16 for VOL00.PO - VOL0F.PO are enough)
#define FTP_MAX_VOL_FILES 16
