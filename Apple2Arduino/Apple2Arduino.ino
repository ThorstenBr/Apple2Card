/* Apple2Arduino firmware */
/* by Daniel L. Marks */

/*
   Copyright (c) 2022 Daniel Marks

  This software is provided 'as-is', without any express or implied
  warranty. In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

/* Note to self to create objdump:

  C:\Users\dmarks\Documents\ArduinoData\packages\arduino\tools\avr-gcc\4.8.1-arduino5\bin\avr-objdump.exe -x -t -s Apple2Arduino.ino.elf > s155143
*/

#include <Arduino.h>
#include <avr/pgmspace.h>
#include <EEPROM.h>
#include "Apple2Arduino.h"
#include "diskio_sdc.h"
#include "mmc_avr.h"
#include "ff.h"
#include "pindefs.h"
#include "ttftp.h"
#include "config.h"
#include "dan2volumes.h"

/*************************************************/
// => See DAN2config.h for configuration options!
/*************************************************/

#ifdef USE_ETHERNET
#include "w5500.h"
#endif

// Include header with selected boot program
#if BOOTPG<=1
  #warning No bootpg selected.
#elif BOOTPG==2
  #include "bootpg_stock_v2.h"   // stock/original bootpg v2 by DL Marks (512bytes)
#elif BOOTPG==3
  #include "bootpg_v3.h"         // improved GUI menu v3, single boot block (512bytes)
#elif BOOTPG==4
  #include "bootpg_cursor_v4.h"  // with cursor keys, v4, two boot blocks (1024bytes)
#elif BOOTPG==5
  #include "bootpg_ipcfg_v5.h"   // with IP configuration option, v6, three boot blocks (1536bytes)
#elif BOOTPG==6
  #include "bootpg_flex_v6.h"   // with flexible volume/SD card mapping
#else
  #error Selected BOOTPG is not implemented.
#endif

#define NUMBER_BOOTBLOCKS ((sizeof(bootblocks)+511)/(512*sizeof(uint8_t)))

#ifdef DEBUG_SERIAL
#ifdef SOFTWARE_SERIAL
 #include <SoftwareSerial.h>
  SoftwareSerial softSerial(SOFTWARE_SERIAL_RX, SOFTWARE_SERIAL_TX);
  #define SERIALPORT() (&softSerial)
#else
  #define SERIALPORT() (&Serial)
#endif
#endif

#define RD_DISK             0 // always read disk
#define RD_FAILSAFE         1 // read disk or boot block, if volume/disk is missing
#define RD_BOOT_BLOCK       2 // always read boot block

#define EEPROM_INIT    0
#define EEPROM_SLOT0   1
#define EEPROM_SLOT1   2
#define EEPROM_A2SLOT  3
#define EEPROM_MAC_IP  4 // bytes 4-9: MAC + IP address for FTP server
#define EEPROM_FREE   10 // next available byte, for future extensions

#ifdef USE_ETHERNET
uint8_t ethernet_initialized = 0;
Wiznet5500 eth(8);
#endif

uint8_t drive_fileno[2];
uint8_t drive_fileno_eeprom[2];
uint8_t a2slot;

uint8_t  unit;
static uint16_t bufaddr; // buffer address in Apple II memory

#ifdef DEBUG_BYTES
uint8_t DEBUG_counter = 0;
uint8_t DEBUG_data[DEBUG_BYTES];
#endif

extern int __heap_start, *__brkval;

extern "C" {
  void write_string(const char *c)
  {
#ifdef DEBUG_SERIAL
    SERIALPORT()->print(c);
    SERIALPORT()->flush();
#endif
  }
}

#ifdef DEBUG_BYTES
void debug_log(uint8_t value)
{
  uint8_t i = 0;
  while ((i<DEBUG_counter)&&(DEBUG_data[i]!=value))
    i++;
  if ((i<DEBUG_BYTES)&&(i==DEBUG_counter)&&(value))
    DEBUG_data[DEBUG_counter++] = value;
}
#endif

void read_eeprom(void)
{
  drive_fileno[0] = drive_fileno[1] = 0; // SD1 volume 0 / SD2 volume 0
  a2slot = 0x7;

  uint8_t init_value = EEPROM.read(EEPROM_INIT);
  if (init_value != 255)
  {
    drive_fileno[0] = EEPROM.read(EEPROM_SLOT0);
    drive_fileno[1] = EEPROM.read(EEPROM_SLOT1);
    // take care of older EPROM contents
    if ((drive_fileno[0] == 255)||(drive_fileno[1] == 255))
    {
       // former "wide mode"
       drive_fileno[0] = 0x00; // SD1, volume 0
       drive_fileno[1] = 0x88; // SD1, volume 8
    }

    a2slot          = EEPROM.read(EEPROM_A2SLOT) & 0x7;

#ifdef USE_FTP
    if (init_value >= 1) // newer version in EEPROM
    {
      // read last byte of MAC + full IP address from EEPROM
      for (uint8_t i=5;i<10;i++)
        FtpMacIpPortData[i] = EEPROM.read(EEPROM_MAC_IP+i);
    }
#endif
  }

  drive_fileno_eeprom[0] = drive_fileno[0];
  drive_fileno_eeprom[1] = drive_fileno[1];
}

void write_eeprom(void)
{
  if (EEPROM.read(EEPROM_SLOT0) != drive_fileno[0])
    EEPROM.write(EEPROM_SLOT0, drive_fileno[0]);
  if (EEPROM.read(EEPROM_SLOT1) != drive_fileno[1])
    EEPROM.write(EEPROM_SLOT1, drive_fileno[1]);
  if (EEPROM.read(EEPROM_A2SLOT) != a2slot)
    EEPROM.write(EEPROM_A2SLOT, a2slot);
  if (EEPROM.read(EEPROM_INIT) == 255)
    EEPROM.write(EEPROM_INIT, 0);
  drive_fileno_eeprom[0] = drive_fileno[0];
  drive_fileno_eeprom[1] = drive_fileno[1];
}

#ifdef USE_FTP
void write_eeprom_ip(void)
{
  // write MAC and IP address to EEPROM
  for (uint8_t i=0;i<10;i++)
  {
    EEPROM.write(EEPROM_MAC_IP+i, FtpMacIpPortData[i]);
  }
  // set EEPROM flag for newer version: we have a valid IP config
  EEPROM.write(EEPROM_INIT, 1);
}
#endif

void setup_pins(void)
{
  INITIALIZE_CONTROL_PORT();
  DISABLE_RXTX_PINS();
  DATAPORT_MODE_RECEIVE();
}

void setup_serial(void)
{
#if (!defined DEBUG_SERIAL)||(defined SOFTWARE_SERIAL)
  DISABLE_RXTX_PINS();
#endif

#ifdef DEBUG_SERIAL
 #ifdef SOFTWARE_SERIAL
  softSerial.begin(9600);
  pinMode(SOFTWARE_SERIAL_RX, INPUT);
  pinMode(SOFTWARE_SERIAL_TX, OUTPUT);
 #else
  Serial.begin(115200);
 #endif
#endif
}

void write_dataport(uint8_t ch)
{
  while (READ_IBFA() != 0);
  DATAPORT_MODE_TRANS();
  WRITE_DATAPORT(ch);
  STB_LOW();
  STB_HIGH();
  DATAPORT_MODE_RECEIVE();
}

uint8_t read_dataport(void)
{
  uint8_t byt;

  while (READ_OBFA() != 0);
  ACK_LOW();
  byt = READ_DATAPORT();
  ACK_HIGH();
  return byt;
}

void get_unit_buf_blk(void)
{
  unit = read_dataport();
#ifdef DEBUG_SERIAL
  SERIALPORT()->print("0000 unit=");
  SERIALPORT()->println(unit, HEX);
#endif
  bufaddr = read_dataport();
  bufaddr |= (((uint16_t)read_dataport()) << 8);
#ifdef DEBUG_SERIAL
  SERIALPORT()->print("0000 buf=");
  SERIALPORT()->println(bufaddr, HEX);
#endif
  request.blk = read_dataport();
  request.blk |= (((uint16_t)read_dataport()) << 8);
#ifdef DEBUG_SERIAL
  SERIALPORT()->print("0000 blk=");
  SERIALPORT()->println(request.blk, HEX);
#endif
}

// take care of special mapping in "ALLVOLS" mode
void calculate_allvols()
{
  uint8_t drive  = (unit >> 7); // access drive 0 or drive 1?

  uint8_t drive1fno = drive_fileno[0];
  uint8_t drive2fno = drive_fileno[1]^0x80;

  if (drive1fno == drive2fno)
  {
    // multiple drive access disabled when both are configured to the same volume
    request.filenum = request.sdslot = 0xff;
    return;
  }

  request.filenum  = ((drive==0) ? drive1fno : drive2fno) & 0x80; // map to the SD card to SD card of drive 1 or drive 2
  request.filenum |= ((unit>>4) & 0x7); // requested file number according to request slot

  if ((drive==1)&&(((drive1fno ^ drive2fno)&0x80) == 0))
  {
    // D2 requested, and both drives are configured to the same SD card = "wide access"
    request.filenum |= 0x8;
  }

  // now some checks to avoid mapping the same drive multiple times
  if ((request.filenum == drive1fno)||(request.filenum == drive2fno))
  {
    request.filenum &= 0x88; // try volume 0 or 8 instead (on current SDcard)
  }

  if ((request.filenum == drive1fno)||(request.filenum == drive2fno))
  {
    // still in conflict? Then don't map any drive for this request...
    request.filenum = request.sdslot = 0xff;
    return;
  }

  // success: properly decode SDcard and file
  request.sdslot   = (request.filenum >> 7) & 0x1;
  request.filenum &= 0xF;
}

// calculate: sdslot and filenum
void calculate_sd_filenum()
{
  uint8_t drive  = (unit >> 7); // access drive 0 or drive 1?

  if ((drive == 1)&&(drive_fileno[0] == (drive_fileno[1]^0x80)))
  {
    // multiple drive access disabled when both are configured to the same volume
    request.filenum = request.sdslot = 0xff;
    return;
  }

  request.filenum = drive_fileno[drive];            // which file to access on this drive by default?
  request.sdslot  = (request.filenum >> 7) ^ drive; // on SD slot 0 or 1?
  request.filenum &= 0x7f;                          // mask file number (get rid of slot selection bit)

  // check if the request was received with the "normal" slot number (otherwise the "ALLVOLS" magic is active)
  uint8_t current_slot = (unit >> 4) & 7;
  if (current_slot != a2slot)
    calculate_allvols();
}

uint8_t do_status(void)
{
  get_unit_buf_blk();
  calculate_sd_filenum();
  uint8_t returncode = (vol_open_drive_file() ? PRODOS_OK : PRODOS_IO_ERR);
  write_dataport(returncode);

  return returncode;
}

#if BOOTPG>1
void read_bootblock(uint8_t* buf)
{
  // check if a custom boot program is available on any SD card (with FAT format)
  if ((slot_type[0] == SLOT_TYPE_FAT)||(slot_type[1] == SLOT_TYPE_FAT))
  {
      request.sdslot  = (slot_type[0] == SLOT_TYPE_FAT) ? 0 : 1;
      request.filenum = 0xff; // use file number 0xff (VOLFF.po)
      if (vol_read_block(buf) == 0) // success?
        return;
  }

  // otherwise return data of builtin boot program
  a2slot = (unit >> 4) & 0x7;

  uint32_t blkofs = request.blk << 9;
  for (uint32_t i = 0; i < 512; i++)
  {
    if (blkofs + i < sizeof(bootblocks))
      buf[i] = pgm_read_byte(&bootblocks[blkofs + i]);
    else
      buf[i] = 0xff;
  }
}
#endif

void do_read(uint8_t rdtype)
{
  uint8_t buf[512];

  get_unit_buf_blk();

  uint8_t returncode = 0;

#if BOOTPG>1
  if (rdtype != RD_BOOT_BLOCK)
#endif
  {
    calculate_sd_filenum();
    returncode = vol_read_block(buf);
  }

#if BOOTPG>1
  if ( (rdtype==RD_BOOT_BLOCK)||
      ((rdtype==RD_FAILSAFE)&&(returncode != 0)))
  {
    // transmit bootpg if requested or volume is missing
    read_bootblock(buf);
    returncode = 0;
  }
#endif

  write_dataport(returncode);

  if (returncode==0)
  {
    DATAPORT_MODE_TRANS();
    for (uint16_t i = 0; i < 512; i++)
    {
      while (READ_IBFA() != 0);
      WRITE_DATAPORT(buf[i]);
      STB_LOW();
      STB_HIGH();
    }
    DATAPORT_MODE_RECEIVE();
  }
}

void do_write(void)
{
  uint8_t returncode = do_status();
  if (returncode != 0)
    return;

  uint8_t buf[512];
  for (uint16_t i = 0; i < 512; i++)
  {
    while (READ_OBFA() != 0);
    ACK_LOW();
    buf[i] = READ_DATAPORT();
    ACK_HIGH();
  }

  calculate_sd_filenum();
  vol_write_block(buf);
}

void do_format(void)
{
  do_status();
}

void write_zeros(uint16_t num)
{
  DATAPORT_MODE_TRANS();
  while (num > 0)
  {
    while (READ_IBFA() != 0);
    WRITE_DATAPORT(0x00);
    STB_LOW();
    STB_HIGH();
    num--;
  }
  DATAPORT_MODE_RECEIVE();
}

void do_set_volume(uint8_t cmd)
{
  // cmd=4: permanently selects drives 0+1, single byte response (used by eprom+bootpg for volume selection)
  // cmd=6: temporarily selects drives 0+1, 512byte response (used by bootpg for volume preview)
  // cmd=7: permanently selects drives 0+1, 512byte response (used by bootpg for volume selection)
  // cmd=8: permanently selects drive 0 only, single byte response (used by eprom for quick access keys)
  get_unit_buf_blk();
  write_dataport(0x00);

  a2slot = (unit >> 4) & 0x7;

  drive_fileno[0] = request.blk & 0xFF;

  if (cmd != 8)
    drive_fileno[1] = (request.blk >> 8);

  if ((drive_fileno[0] == 255)||(drive_fileno[1] == 255))
  {
    // former "wide mode"
    drive_fileno[0] = 0x00; // select volume 0 on SD1
    drive_fileno[1] = 0x88; // select volume 8 on SD1
  }

  if (cmd != 6) // don't update EEPROM for temporary selection
    write_eeprom();

  if ((cmd != 4)&&(cmd != 8)) // dummy 512 byte block response for commands 6+7
    write_zeros(512);
}

void do_get_volume(uint8_t cmd)
{
  // cmd=5: return eeprom volume configuration
  // cmd=9: return current volume selection
  get_unit_buf_blk();
  write_dataport(0x00);
  if (cmd == 5)
  {
    // return persistent configuration (this is what the config utilities want to know)
    write_dataport(drive_fileno_eeprom[0]);
    write_dataport(drive_fileno_eeprom[1]);
  }
  else
  {
    // return current (temporary) configuration
    write_dataport(drive_fileno[0]);
    write_dataport(drive_fileno[1]);
  }
  write_zeros(510);
}

#ifdef USE_ETHERNET
void do_initialize_ethernet(void)
{
  uint8_t mac_address[6];
#ifdef DEBUG_SERIAL
  SERIALPORT()->println("initialize ethernet");
#endif
  for (uint8_t i = 0; i < 6; i++)
  {
    mac_address[i] = read_dataport();
#ifdef DEBUG_SERIAL
    SERIALPORT()->print(mac_address[i], HEX);
    SERIALPORT()->print(" ");
#endif
  }
  mmc_wait_busy_spi(); // make no MMC card is blocking the SPI bus before accessing WIZnet SPI
  if (ethernet_initialized)
    eth.end();
  if (eth.begin(mac_address))
  {
#ifdef DEBUG_SERIAL
    SERIALPORT()->println("initialized");
#endif
    ethernet_initialized = 1;
    write_dataport(0);
    return;
  }
#ifdef DEBUG_SERIAL
  SERIALPORT()->println("not initialized");
#endif
  ethernet_initialized = 0;
  write_dataport(1);
}

void do_poll_ethernet(void)
{
  uint16_t len;
#ifdef DEBUG_SERIAL
  SERIALPORT()->println("poll eth");
#endif
  if (ethernet_initialized)
  {
    len = read_dataport();
    len |= ((uint16_t)read_dataport()) << 8;
#ifdef DEBUG_SERIAL
    SERIALPORT()->print("read len ");
    SERIALPORT()->println(len, HEX);
#endif
    mmc_wait_busy_spi(); // make no MMC card is blocking the SPI bus before accessing WIZnet SPI
    len = eth.readFrame(NULL, len);
#ifdef DEBUG_SERIAL
    SERIALPORT()->print("recv len ");
    SERIALPORT()->println(len, HEX);
#endif
  } else
  {
    write_dataport(0);
    write_dataport(0);
  }
}

void do_send_ethernet(void)
{
  uint16_t len;
#ifdef DEBUG_SERIAL
  SERIALPORT()->println("send eth");
#endif
  if (ethernet_initialized)
  {
    len = read_dataport();
    len |= ((uint16_t)read_dataport()) << 8;
#ifdef DEBUG_SERIAL
    SERIALPORT()->print("len ");
    SERIALPORT()->println(len, HEX);
#endif
    mmc_wait_busy_spi(); // make no MMC card is blocking the SPI bus before accessing WIZnet SPI
    eth.sendFrame(NULL, len);
  }
  write_dataport(0);
}
#endif


#ifdef USE_FTP
void do_set_ip_config(void)
{
  get_unit_buf_blk();

  // use bytes from standard parameters to pass the new IP address (and last byte of Mac address)
  FtpMacIpPortData[5] = unit;              // least significant MAC address byte
  FtpMacIpPortData[6] = bufaddr & 0xff;    // IP address byte 1
  FtpMacIpPortData[7] = bufaddr >> 8;      // IP address byte 2
  FtpMacIpPortData[8] = request.blk & 0xff; // IP address byte 3
  FtpMacIpPortData[9] = request.blk >> 8;   // IP address byte 4
  write_eeprom_ip();

  // success: return 1(!=0) anyway. Sneaky trick - so we can reuse the routine from ROM and prevent it from reading further bytes
  write_dataport(1);
}

void do_get_ip_config(void)
{
  get_unit_buf_blk();
  write_dataport(0x00);
  for (uint8_t i=0;i<10;i++)
    write_dataport(FtpMacIpPortData[i]);
  write_zeros(6); // 6 bytes reserved (in case we want to extend the configuration)
  // we may need to briefly wait for the FTP/Wiznet to be initialized, otherwise we cannot report the proper state
  while (FtpState == 0)
  {
    loopTinyFtp();
  }
  write_dataport((FtpState==-1) ? 0 : 1); // return 1 when FTP/Wiznet present/enabled, 0 otherwise (not present or disabled)
  write_zeros(512-17);
}
#endif

void do_command(uint8_t cmd)
{
  if (cmd == 0xac)
    cmd = read_dataport();
#ifdef DEBUG_SERIAL
  SERIALPORT()->print("0000 cmd=");
  SERIALPORT()->println(cmd, HEX);
#endif
  switch (cmd)
  {
    case 0:    do_status();
      break;
    case 1:    do_read(RD_DISK); // normal read
      break;
    case 2:    do_write();
      break;
    case 3:    do_format();
      break;
    case 4:
    case 6:
    case 7:
    case 8:    do_set_volume(cmd);
      break;
    case 5:
    case 9:    do_get_volume(cmd);
      break;
    case 10:   do_read(RD_FAILSAFE); // failsafe read
      break;
#ifdef USE_ETHERNET
    case 0x10: do_initialize_ethernet();
      break;
    case 0x11: do_poll_ethernet();
      break;
    case 0x12: do_send_ethernet();
      break;
#endif
#ifdef USE_FTP
    case 0x20: do_set_ip_config();
      break;
    case 0x21: do_get_ip_config();
      break;
#endif
#if BOOTPG>1
    case 13+128:
    case 32+128:  do_read(RD_BOOT_BLOCK);
      break;
#endif
    case 0xFF: // intentional illegal command always returning an error, just for synchronisation
    default:      write_dataport(0x27);
      break;
  }
}

int freeRam ()
{
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}

#ifdef USE_MEM_CHECK
// memory overflow check: when the stack runs into the heap area...
void check_memory(int id)
{
  int v;
  if ((__heap_start != 0xBEEF)||  // magic word still there?
      (&v <= &__heap_start))      // or stack already below the heap?
  {
#ifdef DEBUG_SERIAL
    SERIALPORT()->print(id);
    SERIALPORT()->println(F("MEM/STACK OVERFLOW!"));
#endif
    while (1);
  }
}
#endif

// Briefly flash both slot LEDs when no SD card is installed.
// Can be used as a simple notification that the ATMEGA is fine and running,
// when a new DAN][ was built.
// Also provides a feedback when there is an issue with (both) SD cards,
// and nothing was detected.
void no_cards_blink()
{
  // no SD card at all detected?
  if ((slot_type[0] == SLOT_TYPE_NODISK)&&
      (slot_type[1] == SLOT_TYPE_NODISK))
  {
    // briefly blink both slot LEDs
    for (uint8_t i=0;i<6;i++)
    {
      PORTB &= ((i&1) ? ~0x02 : ~0x4);
      delay(150);
      PORTB |= 0x06;
      delay(150);
    }
  }
}

void setup()
{
#ifdef USE_MEM_CHECK
  // marker for stack/memory overflow detection (we're not using heap anyway).
  __heap_start = 0xBEEF;
#endif

  setup_pins();
  setup_serial();
  read_eeprom();

  request.sdslot = SDSLOT1;
  vol_check_sdslot_type();
  request.sdslot = SDSLOT2;
  vol_check_sdslot_type();

  no_cards_blink();

#ifdef DEBUG_SERIAL
  SERIALPORT()->println("0000");
  SERIALPORT()->print(" f=");
  SERIALPORT()->print(freeRam());
  SERIALPORT()->flush();
#endif

  DATAPORT_MODE_RECEIVE();

  /* kludge: when pressing Ctrl-Reset (warmstart), the very first $AC sync byte may be eaten by the bootloader,
     with the next command byte already pending.
     The first ProDOS access after a warmstart is usually a "read disk" command (0x01).
     To prevent issues with very early ProDOS disk reads after a warmstart, we make an exception and accept
     a read-disk command, even without a preceding sync byte ($AC) - but only if this is already pending during
     the setup phase...
  */
  if (READ_OBFA() == 0) // very early command-byte already pending?
  {
    uint8_t instr = read_dataport();
    if ((instr == 0xAC)||(instr == 0x01)) do_command(instr);
  }
}

void loop()
{
#ifdef USE_FTP
 #ifdef USE_ETHERNET
  if (ethernet_initialized==0)  // when slave eth is initialized, we stop FTP processing: the 6502 is now controlling the Wiznet...
 #endif
  {
    // this will temporarily suspend Apple II requests while an FTP connection is busy
    loopTinyFtp();
    CHECK_MEM(1); // memory overflow check (when enabled)
  }
#endif

  while (READ_OBFA() == 0)
  {
    uint8_t instr = read_dataport();
    if (instr == 0xAC) do_command(instr);
    CHECK_MEM(0); // memory overflow check (when enabled)
  }
}
