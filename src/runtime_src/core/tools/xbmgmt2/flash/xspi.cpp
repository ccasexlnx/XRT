/**
 * Copyright (C) 2016-2018 Xilinx, Inc
 * Author(s) : Sonal Santan
 *           : Hem Neema
 *           : Ryan Radjabi
 *
 * Licensed under the Apache License, Version 2.0 (the "License"). You may
 * not use this file except in compliance with the License. A copy of the
 * License is located at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */
#include "xspi.h"
#include "core/common/system.h"
#include "core/common/device.h"
#include "core/common/query_requests.h"
#include <iostream>
#include <string>
#include <fstream>
#include <cassert>
#include <cstring>
#include <climits>
#include <vector>
#include <limits>
#include <array>
#include <fcntl.h>


#include "core/common/error.h"
#include "core/tools/common/XBUtilities.h"
#include "core/tools/common/ProgressBar.h"
namespace XBU = XBUtilities;
#include "boost/format.hpp"

template <typename ...Args>
int
dummy(const char* format, Args&&... args)
{
  printf(format, args ...);
  return 0;
}

#ifdef __GNUC__
# define XSPI_UNUSED __attribute__((unused))
#else
# define XSPI_UNUSED
#endif

#ifdef _WIN32
# pragma warning( disable : 4189 )
#endif

//#define FLASH_BASE_ADDRESS BPI_FLASH_OFFSET
#define PAGE_SIZE 256
static const bool FOUR_BYTE_ADDRESSING = false;

uint32_t MAX_NUM_SECTORS = 0;
uint32_t selected_sector = std::numeric_limits<uint32_t>::max();

//testing sizes.
#define WRITE_DATA_SIZE 128
#define READ_DATA_SIZE 128


#define COMMAND_PAGE_PROGRAM            0x02 /* Page Program command */
#define COMMAND_QUAD_WRITE              0x32 /* Quad Input Fast Program */
#define COMMAND_EXT_QUAD_WRITE          0x38 /* Extended quad input fast program */
#define COMMAND_4KB_SUBSECTOR_ERASE     0x20 /* 4KB Subsector Erase command */
#define COMMAND_32KB_SUBSECTOR_ERASE    0x52 /* 32KB Subsector Erase command */
#define COMMAND_SECTOR_ERASE            0xD8 /* Sector Erase command */
#define COMMAND_BULK_ERASE              0xC7 /* Bulk Erase command */
#define COMMAND_RANDOM_READ             0x03 /* Random read command */
#define COMMAND_DUAL_READ               0x3B /* Dual Output Fast Read */
#define COMMAND_DUAL_IO_READ            0xBB /* Dual IO Fast Read */
#define COMMAND_QUAD_READ               0x6B /* Quad Output Fast Read */
#define COMMAND_QUAD_IO_READ            0xEB /* Quad IO Fast Read */
#define COMMAND_IDCODE_READ             0x9F /* Read ID Code */
//read commands
#define COMMAND_STATUSREG_READ               0x05 /* Status read command */
#define COMMAND_FLAG_STATUSREG_READ          0x70 /* Status flag read command */
#define COMMAND_NON_VOLATILE_CFGREG_READ     0xB5 /* Non volatile configuration register read command */
#define COMMAND_VOLATILE_CFGREG_READ         0x85 /* Volatile configuration register read command */
#define COMMAND_ENH_VOLATILE_CFGREG_READ     0x65 /* Enhanced volatile configuration register read command */
#define COMMAND_EXTENDED_ADDRESS_REG_READ    0xC8 /* Enhanced volatile configuration register read command */
//write commands
#define COMMAND_STATUSREG_WRITE              0x01 /* Status read command */
#define COMMAND_NON_VOLATILE_CFGREG_WRITE    0xB1 /* Non volatile configuration register read command */
#define COMMAND_VOLATILE_CFGREG_WRITE        0x81 /* Volatile configuration register read command */
#define COMMAND_ENH_VOLATILE_CFGREG_WRITE    0x61 /* Enhanced volatile configuration register read command */
#define COMMAND_EXTENDED_ADDRESS_REG_WRITE   0xC5 /* Enhanced volatile configuration register read command */

#define COMMAND_CLEAR_FLAG_REGISTER          0x50 /* Clear flag register */

//4-byte addressing
#define ENTER_FOUR_BYTE_ADDR_MODE               0xB7 /* enter 4-byte address mode */
#define EXIT_FOUR_BYTE_ADDR_MODE                0xE9 /* exit 4-byte address mode */
#define FOUR_BYTE_READ                          0x13 /* 4-byte read */
#define FOUR_BYTE_FAST_READ                     0x0C /* 4-byte fast read */
#define FOUR_BYTE_DUAL_OUTPUT_FAST_READ         0x3C /* 4-byte dual output fast read */
#define FOUR_BYTE_DUAL_IO_FAST_READ             0xBC /* 4-byte dual Input/output fast read */
#define FOUR_BYTE_QUAD_OUTPUT_FAST_READ         0x6C /* 4-byte quad output fast read */
#define FOUR_BYTE_QUAD_IO_FAST_READ             0xEC /* 4-byte quad output fast read */
#define FOUR_BYTE_PAGE_PROGRAM                  0x12 /* 4-byte page program */
#define FOUR_BYTE_QUAD_INPUT_FAST_PROGRAM       0x34 /* 4-byte quad input fast program */
#define FOUR_BYTE_QUAD_INPUT_EXT_FAST_PROGRAM   0x3E /* 4-byte quad input extended fast program */
#define FOUR_BYTE_SECTOR_ERASE                  0xDC /* 4-byte sector erase */

static const unsigned int READ_WRITE_EXTRA_BYTES = FOUR_BYTE_ADDRESSING ? 5 :4;
static const unsigned int  SECTOR_ERASE_BYTES = FOUR_BYTE_ADDRESSING ? 5 :4;


#define IDCODE_READ_BYTES              5

#define DUAL_READ_DUMMY_BYTES           2
#define QUAD_READ_DUMMY_BYTES           4
#define DUAL_IO_READ_DUMMY_BYTES        2
#define QUAD_IO_READ_DUMMY_BYTES        5

//#define READ_WRITE_EXTRA_BYTES          4 /* Read/Write extra bytes */
//#define SECTOR_ERASE_BYTES              4 /* Sector erase extra bytes */
#define WRITE_ENABLE_BYTES              1 /* Write Enable bytes */
#define BULK_ERASE_BYTES                1 /* Bulk erase extra bytes */
#define STATUS_READ_BYTES               2 /* Status read bytes count */
#define STATUS_WRITE_BYTES              2 /* Status write bytes count */



#define NUM_SLAVES 2
#define SLAVE_SELECT_MASK ((1 << NUM_SLAVES) -1)
/*
 * Flash not busy mask in the status register of the flash device.
 */
#define FLASH_SR_IS_READY_MASK          0x01 /* Ready mask */
#define COMMAND_WRITE_ENABLE        0x06 /* Write Enable command */

//SPI control reg masks.
#define XSP_CR_LOOPBACK_MASK       0x00000001 /**< Local loopback mode */
#define XSP_CR_ENABLE_MASK         0x00000002 /**< System enable */
#define XSP_CR_MASTER_MODE_MASK    0x00000004 /**< Enable master mode */
#define XSP_CR_CLK_POLARITY_MASK   0x00000008 /**< Clock polarity high or low */
#define XSP_CR_CLK_PHASE_MASK      0x00000010 /**< Clock phase 0 or 1 */
#define XSP_CR_TXFIFO_RESET_MASK   0x00000020 /**< Reset transmit FIFO */
#define XSP_CR_RXFIFO_RESET_MASK   0x00000040 /**< Reset receive FIFO */
#define XSP_CR_MANUAL_SS_MASK      0x00000080 /**< Manual slave select assert */
#define XSP_CR_TRANS_INHIBIT_MASK  0x00000100 /**< Master transaction inhibit */

/**
 * LSB/MSB first data format select. The default data format is MSB first.
 * The LSB first data format is not available in all versions of the Xilinx Spi
 * Device whereas the MSB first data format is supported by all the versions of
 * the Xilinx Spi Devices. Please check the HW specification to see if this
 * feature is supported or not.
 */
#define XSP_CR_LSB_MSB_FIRST_MASK       0x00000200

//End SPI CR masks

//SPI status reg masks
#define XSP_SR_RX_EMPTY_MASK       0x00000001 /**< Receive Reg/FIFO is empty */
#define XSP_SR_RX_FULL_MASK        0x00000002 /**< Receive Reg/FIFO is full */
#define XSP_SR_TX_EMPTY_MASK       0x00000004 /**< Transmit Reg/FIFO is empty */
#define XSP_SR_TX_FULL_MASK        0x00000008 /**< Transmit Reg/FIFO is full */
#define XSP_SR_MODE_FAULT_MASK     0x00000010 /**< Mode fault error */
#define XSP_SR_SLAVE_MODE_MASK     0x00000020 /**< Slave mode select */

/*
 * The following bits are available only in axi_qspi Status register.
 */
#define XSP_SR_CPOL_CPHA_ERR_MASK  0x00000040 /**< CPOL/CPHA error */
#define XSP_SR_SLAVE_MODE_ERR_MASK 0x00000080 /**< Slave mode error */
#define XSP_SR_MSB_ERR_MASK        0x00000100 /**< MSB Error */
#define XSP_SR_LOOP_BACK_ERR_MASK  0x00000200 /**< Loop back error */
#define XSP_SR_CMD_ERR_MASK        0x00000400 /**< 'Invalid cmd' error */


//End SPI SR masks

#define XSP_SRR_OFFSET          0x40    /**< Software Reset register */
#define XSP_CR_OFFSET           0x60    /**< Control register */
#define XSP_SR_OFFSET           0x64    /**< Status Register */
#define XSP_DTR_OFFSET          0x68    /**< Data transmit */
#define XSP_DRR_OFFSET          0x6C    /**< Data receive */
#define XSP_SSR_OFFSET          0x70    /**< 32-bit slave select */
#define XSP_TFO_OFFSET          0x74    /**< Tx FIFO occupancy */
#define XSP_RFO_OFFSET          0x78    /**< Rx FIFO occupancy */

#define BYTE1               0 /* Byte 1 position */
#define BYTE2               1 /* Byte 2 position */
#define BYTE3               2 /* Byte 3 position */
#define BYTE4               3 /* Byte 4 position */
#define BYTE5               4 /* Byte 5 position */
#define BYTE6               5 /* Byte 6 position */
#define BYTE7               6 /* Byte 7 position */
#define BYTE8               7 /* Byte 8 position */

//JEDEC vendor IDs
#define MICRON_VENDOR_ID   0x20
#define MACRONIX_VENDOR_ID 0xC2

/**
 * SPI Software Reset Register (SRR) mask.
 */
#define XSP_SRR_RESET_MASK              0x0000000A

// Bitstream guard information
#define NOOP        0x00000020//0x20000000
#define DUMMY       0xFFFFFFFF
#define BUSWIDTH1   0xBB000000//0x000000BB
#define BUSWIDTH2   0x44002211//0x11220044
#define SYNC        0x665599AA//0xAA995566
#define TIMER       0x01200230//0x30022001
#define WDT_ENABLE  0x02000040//0x40000002
#define CFG_CMD     0x01800030//0x30008001
#define LTIMER      0x11000000//0x00000011
#define FLASH_BASE  0x040000
#define BITSTREAM_GUARD_SIZE 0x1000
uint32_t BITSTREAM_GUARD[] = {
            DUMMY,
            BUSWIDTH1,
            BUSWIDTH2,
            DUMMY,
            DUMMY,
            SYNC,
            NOOP,
            TIMER,
            WDT_ENABLE,
            NOOP,
            NOOP
};

//----
#define XSpi_ReadReg(RegOffset) readReg(RegOffset)
#define XSpi_WriteReg(RegOffset, RegisterValue) writeReg(RegOffset, RegisterValue)

#define XSpi_SetControlReg(Mask) XSpi_WriteReg(XSP_CR_OFFSET, (Mask))
#define XSpi_GetControlReg() XSpi_ReadReg(XSP_CR_OFFSET)

#define XSpi_GetStatusReg() XSpi_ReadReg(XSP_SR_OFFSET)

#define XSpi_SetSlaveSelectReg(Mask) XSpi_WriteReg(XSP_SSR_OFFSET, (Mask))
#define XSpi_GetSlaveSelectReg() XSpi_ReadReg(XSP_SSR_OFFSET)

//---

static uint8_t WriteBuffer[PAGE_SIZE + READ_WRITE_EXTRA_BYTES];
static uint8_t ReadBuffer[PAGE_SIZE + READ_WRITE_EXTRA_BYTES + 4];

static int slave_index = 0;

static std::array<int,2> flashVendors = {
    MICRON_VENDOR_ID,
    MACRONIX_VENDOR_ID
};
static int flashVendor = -1;

static bool TEST_MODE = false;
static bool TEST_MODE_MCS_ONLY = false;

static const uint32_t CONTROL_REG_START_STATE =  XSP_CR_TRANS_INHIBIT_MASK | XSP_CR_MANUAL_SS_MASK |XSP_CR_RXFIFO_RESET_MASK
        | XSP_CR_TXFIFO_RESET_MASK | XSP_CR_ENABLE_MASK | XSP_CR_MASTER_MODE_MASK ;

static void clearReadBuffer(unsigned int size) {
    for(unsigned int i =0; i < size; ++i) {
        ReadBuffer[i] = 0;
    }
}

static void clearWriteBuffer(unsigned int size) {
    for(unsigned int i =0; i < size; ++i) {
        WriteBuffer[i] = 0;
    }
}

static void clearBuffers() {
    clearReadBuffer(PAGE_SIZE + READ_WRITE_EXTRA_BYTES+4);
    clearWriteBuffer(PAGE_SIZE + READ_WRITE_EXTRA_BYTES);
}

//Helper functions to interleave/deinterleave nibbles
static void stripe_data(uint8_t *intrlv_buf, uint8_t *buf0, uint8_t *buf1, uint32_t num_bytes) {
    for(uint32_t i=0; i<num_bytes; i=i+2) {
        buf0[i/2] = (intrlv_buf[i] << 4) | (intrlv_buf[i+1] & 0x0F);
        buf1[i/2] = (intrlv_buf[i] & 0xF0) | (intrlv_buf[i+1] >> 4);
    }
}

/*
 * Chronos sleep resolution on Windows is 1 ms which is exponentially bigger than
 * the required sleep. This is a busy loop to mimick the accurate amount of sleep.
 */
static void delay(std::chrono::microseconds us)
{
	std::chrono::high_resolution_clock::time_point currTime;
	const std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();
	do {
		currTime = std::chrono::high_resolution_clock::now();
	} while (std::chrono::duration<double>(currTime - startTime) < us);
}

XSPI_Flasher::~XSPI_Flasher()
{
    if (mFlashDev)
        std::fclose(mFlashDev);
}

XSPI_Flasher::XSPI_Flasher(std::shared_ptr<xrt_core::device> dev)
{
    mDev = dev;

    std::string err;
    flash_base = 0;
    try {
        flash_base = xrt_core::device_query<xrt_core::query::flash_bar_offset>(mDev.get());
    }
    catch (...) {}
    if (flash_base == 0)
        flash_base = FLASH_BASE;

    mFlashDev = nullptr;
#ifdef __GNUC__
    if (std::getenv("FLASH_VIA_USER") == NULL) {
        int fd = mDev->open("flash", O_RDWR);
        if (fd >= 0)
            mFlashDev = fdopen(fd, "r+");
        if (mFlashDev == NULL)
            std::cout << "Failed to open flash device on card" << std::endl;
    }
#endif
}

static bool isDualQSPI(xrt_core::device *dev) {
    auto deviceID = xrt_core::device_query<xrt_core::query::pcie_device>(dev);
    return (deviceID == 0xE987 || deviceID == 0x6987 || deviceID == 0xD030 ||
            deviceID == 0xF987);
}

unsigned int XSPI_Flasher::getSector(unsigned int address) {
    return (address >> 24) & 0xF;
}

bool XSPI_Flasher::setSector(unsigned int address) {
    uint32_t sector = getSector(address);
    //Select sector before
    if(sector >= MAX_NUM_SECTORS) {
        std::cout << "ERROR: Invalid sector encountered" << std::endl;
        std::cout << "ERROR: Bad address 0x" << std::hex << address << std::dec << std::endl;
        return false;
    } else if(sector == selected_sector) //Don't do anything if its already selected
        return true;

    if(!writeRegister(COMMAND_EXTENDED_ADDRESS_REG_WRITE, sector, 1))
        return false;
    else {
        selected_sector = sector;
        return true;
    }
}

int XSPI_Flasher::xclTestXSpi(int index)
{
    TEST_MODE = true;

    if(TEST_MODE_MCS_ONLY) {
        //just test the mcs.
        return 0;
    }

    //2 slaves present, set the slave index.
    slave_index = index;

    //print the IP (not of flash) control/status register.
    uint32_t ControlReg = XSpi_GetControlReg();
    uint32_t StatusReg = XSpi_GetStatusReg();
    std::cout << "Boot IP Control/Status " << std::hex << ControlReg << "/" << StatusReg << std::dec << std::endl;

    //Make sure it is ready to receive commands.
    ControlReg = XSpi_GetControlReg();
    ControlReg = CONTROL_REG_START_STATE;

    XSpi_SetControlReg(ControlReg);
    ControlReg = XSpi_GetControlReg();
    StatusReg = XSpi_GetStatusReg();
    std::cout << "Reset IP Control/Status " << std::hex << ControlReg << "/" << StatusReg << std::dec << std::endl;

    //1. Testing idCode reads.
    //--
    std::cout << "Testing id code " << std::endl;
    if(!getFlashId()) {
        std::cout << "Exiting now, as could not get correct idcode" << std::endl;
        exit(-EOPNOTSUPP);
    }

    std::cout << "id code successful (please verify the idcode output too" << std::endl;
    std::cout << "Now reading various flash registers" << std::endl;

    //2. Testing register reads.
    //Using STATUS_READ_BYTES 2 for all, TODO ?
    uint8_t Cmd = COMMAND_STATUSREG_READ;
    std::cout << "Testing COMMAND_STATUSREG_READ" << std::endl;
    readRegister(Cmd, STATUS_READ_BYTES);

    std::cout << "Testing COMMAND_FLAG_STATUSREG_READ" << std::endl;
    Cmd = COMMAND_FLAG_STATUSREG_READ;
    readRegister(Cmd, STATUS_READ_BYTES);

    std::cout << "Testing COMMAND_NON_VOLATILE_CFGREG_READ" << std::endl;
    Cmd = COMMAND_NON_VOLATILE_CFGREG_READ;
    readRegister(Cmd, 4);

    std::cout << "Testing COMMAND_VOLATILE_CFGREG_READ" << std::endl;
    Cmd = COMMAND_VOLATILE_CFGREG_READ;
    readRegister(Cmd, STATUS_READ_BYTES);

    std::cout << "Testing COMMAND_ENH_VOLATILE_CFGREG_READ" << std::endl;
    Cmd = COMMAND_ENH_VOLATILE_CFGREG_READ;
    readRegister(Cmd, STATUS_READ_BYTES);

    std::cout << "Testing COMMAND_EXTENDED_ADDRESS_REG_READ" << std::endl;
    Cmd = COMMAND_EXTENDED_ADDRESS_REG_READ;
    readRegister(Cmd, STATUS_READ_BYTES);

    //3. Testing simple read and write
    std::cout << "Testing read and write of 16 bytes" << std::endl;

    //unsigned int baseAddr = 0x007A0000;
    unsigned int baseAddr = 0;
    unsigned int Addr = 0;
    unsigned int AddressBytes = 3;
    if(FOUR_BYTE_ADDRESSING) {
        AddressBytes = 4;
        writeRegister(ENTER_FOUR_BYTE_ADDR_MODE, 0, 0);
    }else
        writeRegister(EXIT_FOUR_BYTE_ADDR_MODE, 0, 0);

    //Verify 3 or 4 byte addressing, 0th bit == 1 => 4 byte.
    std::cout << "Testing COMMAND_FLAG_STATUSREG_READ" << std::endl;
    Cmd = COMMAND_FLAG_STATUSREG_READ;
    readRegister(Cmd, STATUS_READ_BYTES);

    XSPI_UNUSED uint8_t WriteCmd = 0xff;
    XSPI_UNUSED uint8_t ReadCmd = 0xff;

    //Test the higher two sectors - first test erase.

    //First try erasing a sector and reading a
    //page (we should get FFFF ...)
    for(unsigned int sector = 2 ; sector <= 3; sector++)
    {
        clearBuffers();

        if(!writeRegister(COMMAND_EXTENDED_ADDRESS_REG_WRITE, sector, 1))
            return false;

        std::cout << "Testing COMMAND_EXTENDED_ADDRESS_REG_READ" << std::endl;
        Cmd = COMMAND_EXTENDED_ADDRESS_REG_READ;
        readRegister(Cmd, STATUS_READ_BYTES);

        //Sector Erase will reset TX and RX FIFO
        if(!sectorErase(Addr + baseAddr, COMMAND_4KB_SUBSECTOR_ERASE))
            return false;

        bool ready = isFlashReady();
        if(!ready){
            std::cout << "Unable to get flash ready" << std::endl;
            return false;
        }

        //try faster read.
        if(FOUR_BYTE_ADDRESSING) {
            ReadCmd = FOUR_BYTE_QUAD_OUTPUT_FAST_READ;
        }else
            ReadCmd = COMMAND_QUAD_READ;

        //if(!readPage(Addr, ReadCmd))
        if(!readPage(Addr + baseAddr))
            return false;
    }

    clearBuffers();
    //---Erase test done


    //---Now try writing and reading a page.
    //first write 2 pages (using 4 128Mb writes) each to 2 sectors, and then read them

    //Write data
    for(unsigned int sector = 2 ; sector <= 3; sector++)
    {
        if(!writeRegister(COMMAND_EXTENDED_ADDRESS_REG_WRITE, sector, 1))
            return false;

        std::cout << "Testing COMMAND_EXTENDED_ADDRESS_REG_READ" << std::endl;
        Cmd = COMMAND_EXTENDED_ADDRESS_REG_READ;
        readRegister(Cmd, STATUS_READ_BYTES);

        for(int j = 0; j < 4; ++j)
        {
            clearBuffers();
            for(unsigned int i = 0; i < WRITE_DATA_SIZE; ++ i) {
              WriteBuffer[i+ AddressBytes + 1] = static_cast<uint8_t>(j + sector + i); //some random data.
            }

            Addr = baseAddr + WRITE_DATA_SIZE*j;

            if(!writePage(Addr)) {
                std::cout << "Write page unsuccessful, returning" << std::endl;
                return -ENXIO;
            }
        }

    }

    clearBuffers();

    //Read the data back, use 2 reads each of 128 bytes, twice to test 2 pages.
    for(unsigned int sector = 2 ; sector <= 3; sector++)
    {
        //Select a sector (sector 2)
        if(!writeRegister(COMMAND_EXTENDED_ADDRESS_REG_WRITE, sector, 1))
            return false;

        std::cout << "Testing COMMAND_EXTENDED_ADDRESS_REG_READ" << std::endl;
        Cmd = COMMAND_EXTENDED_ADDRESS_REG_READ;
        readRegister(Cmd, STATUS_READ_BYTES);

        //This read should be mix of a b c .. and Z Y X ...
        for(int j = 0 ; j < 4; ++j)
        {
            clearBuffers();
            Addr = baseAddr + WRITE_DATA_SIZE*j;
            if(!readPage(Addr)) {
                std::cout << "Read page unsuccessful, returning" << std::endl;
                return -ENXIO;
            }
        }
        std::cout << "Done reading sector: " << sector << std::endl;
    }

    return 0;
}

//Method for updating QSPI (expects 1 MCS files)
int XSPI_Flasher::xclUpgradeFirmware1(std::istream& mcsStream1) {
    int status = 0;
    uint32_t bitstream_start_loc = 0, bitstream_shift_addr = 0;

    if (mFlashDev)
        return upgradeFirmware1Drv(mcsStream1);

    //Parse MCS file for first flash device
    status = parseMCS(mcsStream1);
    if(status)
        return status;

    //Get bitstream start location
    bitstream_start_loc = recordList.front().mStartAddress;

    //Write bitstream guard if MCS file is not at address 0
    if(bitstream_start_loc != 0) {
        if(!writeBitstreamGuard(bitstream_start_loc))
            throw xrt_core::error("Unable to set bitstream guard!");
        bitstream_shift_addr += BITSTREAM_GUARD_SIZE;
        std::cout << boost::format("%-8s : %s \n") % "INFO" % "Enabled bitstream guard";
        std::cout << boost::format("%-8s : %s\n") % "INFO" % "Bitstream will not be loaded until flashing is finished";
    }

    //Set slave index to 0
    if (!prepareXSpi(0))
        throw xrt_core::error("Unable to prepare the flash chip");

    //Program MCS file
    status = programXSpi(mcsStream1, bitstream_shift_addr);
    if(status)
        return status;

    //Finally we clear bitstream guard if not writing to address 0
    //This will allow the bitstream to be loaded
    if(bitstream_start_loc != 0) {
        if(!clearBitstreamGuard(bitstream_start_loc))
            throw xrt_core::error("Unable to clear bitstream guard!");
        std::cout << boost::format("%-8s : %s\n") % "INFO" % "Cleared bitstream guard. Bitstream now active.";
    }

    return 0;
}


//Method for updating dual QSPI (expects 2 MCS files)
int XSPI_Flasher::xclUpgradeFirmware2(std::istream& mcsStream1, std::istream& mcsStream2) {
    int status = 0;
    uint32_t bitstream_start_loc = 0, bitstream_shift_addr = 0;

    if (!isDualQSPI(mDev.get())) {
        std::cout << "ERROR: Device does not support dual QSPI!" << std::endl;
        exit(-EINVAL);
    }

    if (mFlashDev)
        return upgradeFirmware2Drv(mcsStream1, mcsStream2);

    //Parse MCS file for first flash device
    status = parseMCS(mcsStream1);
    if(status)
        return status;

    //Get bitstream start location
    bitstream_start_loc = recordList.front().mStartAddress;

    //Write bitstream guard if MCS file is not at address 0
    if(bitstream_start_loc != 0) {
        if(!writeBitstreamGuard(bitstream_start_loc)) {
            std::cout << "ERROR: Unable to set bitstream guard!" << std::endl;
            return -EINVAL;
        }
        bitstream_shift_addr += BITSTREAM_GUARD_SIZE;
        std::cout << "Enabled bitstream guard. Bitstream will not be loaded until flashing is finished." << std::endl;
    }

    //Set slave index to 0
    std::cout << "Preparing flash chip 0" << std::endl;
    if (!prepareXSpi(0)) {
        std::cout << "ERROR: Unable to prepare the flash chip 0\n";
        return -EINVAL;
    }
    //Program first MCS file
    status = programXSpi(mcsStream1, bitstream_shift_addr);
    if(status)
        return status;

    //Parse MCS file for second flash device
    status = parseMCS(mcsStream2);
    if(status)
        return status;

    //Set slave index to 1
    std::cout << "Preparing flash chip 1" << std::endl;
    if (!prepareXSpi(1)) {
        std::cout << "ERROR: Unable to prepare the flash chip 1\n";
        return -EINVAL;
    }
    //Program second MCS file
    status = programXSpi(mcsStream2, bitstream_shift_addr);
    if(status)
        return status;

    //Finally we clear bitstream guard if not writing to address 0
    //This will allow the bitstream to be loaded
    if(bitstream_start_loc != 0) {
        if(!clearBitstreamGuard(bitstream_start_loc)) {
            std::cout << "ERROR: Unable to clear bitstream guard!" << std::endl;
            return -EINVAL;
        }
        std::cout << "Cleared bitstream guard. Bitstream now active." << std::endl;
    }

    return 0;
}

int XSPI_Flasher::parseMCS(std::istream& mcsStream) {
    clearBuffers();
    recordList.clear();

    std::string startAddress;
    ELARecord record;
    bool endRecordFound = false;

    int lineno = 0;
    while (!mcsStream.eof() && !endRecordFound) {
        lineno++;
        std::string line;
        std::getline(mcsStream, line);
        if (line.size() == 0) {
            continue;
        }
        if (line[0] != ':') {
            return -EINVAL;
        }
        const unsigned int dataLen = std::stoi(line.substr(1, 2), 0 , 16);
        const unsigned int address = std::stoi(line.substr(3, 4), 0, 16);
        const unsigned int recordType = std::stoi(line.substr(7, 2), 0 , 16);
        switch (recordType) {
        case 0x00:
        {
            if (dataLen > 16) {
                // For xilinx mcs files data length should be 16 for all records
                // except for the last one which can be smaller
                return -EINVAL;
            }
            if (address != (record.mDataCount+(record.mStartAddress & 0xFFFF))) {
                if(record.mDataCount == 0) {
                    //First entry only.
                    assert(record.mStartAddress != 0);
                    assert(record.mEndAddress != 0);
                    record.mStartAddress += address;
                    record.mEndAddress += address;
                }else {
                    std::cout << "Address is not contiguous ! " << std::endl;
                    return -EINVAL;
                }
            }
            //if ( ((record.mEndAddress-record.mStartAddress)& 0xFFFF) != address) {
              //  return -EINVAL;
            //}
            record.mDataCount += dataLen;
            record.mEndAddress += dataLen;
            break;
        }
        case 0x01:
        {
            if (startAddress.size() == 0) {
                break;
            }
            recordList.push_back(record);
            endRecordFound = true;
            break;
        }
        case 0x02:
        {
            assert(0);
            break;
        }
        case 0x04:
        {
            if (address != 0x0) {
                return -EINVAL;
            }
            if (dataLen != 2) {
                return -EINVAL;
            }
            std::string newAddress = line.substr(9, dataLen * 2);
            if (startAddress.size()) {
                // Finish the old record
                recordList.push_back(record);
            }
            // Start a new record
            record.mStartAddress = std::stoi(newAddress, 0 , 16);
            record.mStartAddress <<= 16;
            record.mDataPos = mcsStream.tellg();
            record.mEndAddress = record.mStartAddress;
            record.mDataCount = 0;
            startAddress = newAddress;
        }
        }
    }

    mcsStream.seekg(0);
    std::cout << boost::format("%-8s : %s %s %s\n") % "INFO" % "Found" % recordList.size() % "ELA records";
    return 0;
}

unsigned int XSPI_Flasher::readReg(unsigned int RegOffset)
{
    unsigned int value = 0;
    mDev->read(flash_base + RegOffset, &value, 4);
    return value;
}

int XSPI_Flasher::writeReg(unsigned int RegOffset, unsigned int value)
{
    mDev->write(flash_base + RegOffset, &value, 4);
    return 0;
}


bool XSPI_Flasher::waitTxEmpty() {
    std::chrono::high_resolution_clock::time_point currTime;
	const std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();
	do {
        uint32_t StatusReg = XSpi_GetStatusReg();
        if(StatusReg & XSP_SR_TX_EMPTY_MASK )
            return true;
        //If not empty, check how many bytes remain.
        uint32_t Data = XSpi_ReadReg(XSP_TFO_OFFSET);
        std::cout << std::hex << Data << std::dec << std::endl;
        delay(std::chrono::microseconds(5));
        currTime = std::chrono::high_resolution_clock::now();
	} while (std::chrono::duration<double>(currTime - startTime) < std::chrono::seconds(3));

    throw xrt_core::error("Unable to get Tx Empty");
}

bool XSPI_Flasher::isFlashReady() {
    uint32_t StatusReg;
    std::chrono::high_resolution_clock::time_point currTime;
	const std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();
	do {
        WriteBuffer[BYTE1] = COMMAND_STATUSREG_READ;
        bool status = finalTransfer(WriteBuffer, ReadBuffer, STATUS_READ_BYTES);
        if(!status)
            throw xrt_core::error("Unable to get Flash Ready");
        StatusReg = ReadBuffer[1];
        if((StatusReg & FLASH_SR_IS_READY_MASK) == 0)
            return true;
        //TODO: Try resetting. Uncomment next line?
        //XSpi_WriteReg(XSP_SRR_OFFSET, XSP_SRR_RESET_MASK);
        delay(std::chrono::microseconds(5));
        currTime = std::chrono::high_resolution_clock::now();
	} while (std::chrono::duration<double>(currTime - startTime) < std::chrono::seconds(3));

    throw xrt_core::error("Unable to get Flash Ready");
}

bool XSPI_Flasher::sectorErase(unsigned int Addr, uint8_t erase_cmd) {
    if(!isFlashReady())
        return false;

    if(!FOUR_BYTE_ADDRESSING) {
        //Select sector when only using 24bit address
        if(!setSector(Addr)) {
            std::cout << "ERROR: Unable to set sector for sectorErase cmd" << std::endl;
            return false;
        }
    }

    if(!writeEnable())
        return false;

    if(TEST_MODE) {
        std::cout << "Testing COMMAND_FLAG_STATUSREG_READ" << std::endl;
        readRegister(COMMAND_FLAG_STATUSREG_READ, STATUS_READ_BYTES);
    }

    uint32_t ControlReg = XSpi_GetControlReg();
    ControlReg |= XSP_CR_RXFIFO_RESET_MASK ;
    ControlReg |= XSP_CR_TXFIFO_RESET_MASK;
    XSpi_SetControlReg(ControlReg);

    /*
    * Prepare the WriteBuffer.
    */
    if(!FOUR_BYTE_ADDRESSING) {
        WriteBuffer[BYTE1] = erase_cmd;
        WriteBuffer[BYTE2] = (uint8_t) (Addr >> 16);
        WriteBuffer[BYTE3] = (uint8_t) (Addr >> 8);
        WriteBuffer[BYTE4] = (uint8_t) (Addr);
    }else {
        WriteBuffer[BYTE1] = erase_cmd;
        WriteBuffer[BYTE2] = (uint8_t) (Addr >> 24);
        WriteBuffer[BYTE3] = (uint8_t) (Addr >> 16);
        WriteBuffer[BYTE4] = (uint8_t) (Addr >> 8);
        WriteBuffer[BYTE5] = (uint8_t) Addr;
    }

    if(!finalTransfer(WriteBuffer, NULL, SECTOR_ERASE_BYTES))
        return false;

    /*
    * Wait till the Transfer is complete and check if there are any errors
    * in the transaction..
    */
    if(!waitTxEmpty())
        return false;

    return true;
}

bool XSPI_Flasher::bulkErase()
{
    if(!isFlashReady())
        return false;

    if(!writeEnable())
        return false;

    uint32_t ControlReg = CONTROL_REG_START_STATE;
    XSpi_SetControlReg(ControlReg);

    XSPI_UNUSED uint32_t testControlReg = XSpi_GetControlReg();
    XSPI_UNUSED uint32_t testStatusReg = XSpi_GetStatusReg();

    //2
    WriteBuffer[BYTE1] = COMMAND_BULK_ERASE;

    if(!finalTransfer(WriteBuffer, NULL, BULK_ERASE_BYTES))
        return false;

    return waitTxEmpty();
}

// Erases the entire flash
bool XSPI_Flasher::fullErase() {
    const uint8_t numFlash = isDualQSPI(mDev.get()) ? 2 : 1;

    for (uint8_t i = 0; i < numFlash; ++i) {
        if (!prepareXSpi(i))
            return false;

        // Go through and erase every sector. Alternatively use bulk or die
        // erase depending on whether the device is monolithic or stacked.
        uint32_t beatCount = 0;
        std::cout << "Erasing flash " << i << std::endl;
        for (uint32_t j = 0; j < MAX_NUM_SECTORS << 24; j += 1 << 15) {
            // Beat every 4MB
            if (++beatCount % (1 << 7) == 0) {
                std::cout << "." << std::flush;
            }

            if (!sectorErase(j, COMMAND_32KB_SUBSECTOR_ERASE)) {
                std::cout << "\nERROR: Failed to erase subsector!" << std::endl;
                return false;
            }
        }

        std::cout << std::endl;
    }

    return true;
}

//Bitstream guard protects from partially programmed bitstreams
bool XSPI_Flasher::writeBitstreamGuard(unsigned int Addr) {
    unsigned char buf[WRITE_DATA_SIZE], buf1[WRITE_DATA_SIZE/2], buf2[WRITE_DATA_SIZE/2];
    unsigned char* write_buffer = &WriteBuffer[READ_WRITE_EXTRA_BYTES];

    if(!isDualQSPI(mDev.get())) {
        memset(buf, 0xFF, sizeof(buf));
        memcpy(buf, BITSTREAM_GUARD, sizeof(BITSTREAM_GUARD));

        if (!prepareXSpi(0)) {
            std::cout << "ERROR: Unable to prepare the flash chip 0\n";
            return false;
        }
        //Clear whatever was at bitstream guard location
        if(!sectorErase(Addr, COMMAND_4KB_SUBSECTOR_ERASE))
            return false;

        //We skip the first page of 4KB subsector so that there's a page of dummy words before bitstream guard
        memcpy(write_buffer, buf, sizeof(buf));
        return writePage(Addr+WRITE_DATA_SIZE);
    } else {
        //Stripe data to separate nibbles for each flash chip
        memset(buf, 0xFF, sizeof(buf));
        memcpy(buf, BITSTREAM_GUARD, sizeof(BITSTREAM_GUARD));
        stripe_data(buf, buf1, buf2, sizeof(buf));

        //Select flash chip 0
        if (!prepareXSpi(0)) {
            std::cout << "ERROR: Unable to prepare the flash chip 0\n";
            return false;
        }
        //Clear whatever was at bitstream guard location
        if(!sectorErase(Addr, COMMAND_4KB_SUBSECTOR_ERASE))
            return false;

        //We skip the first page of 4KB subsector so that there's a page of dummy words before bitstream guard
        memcpy(write_buffer, buf1, sizeof(buf)/2);
        if(!writePage(Addr+WRITE_DATA_SIZE)) {
            std::cout << "ERROR: Unable to write bitstream guard to flash chip 1!\n";
            return false;
        }

        //Select flash chip 1
        if (!prepareXSpi(1)) {
            std::cout << "ERROR: Unable to prepare the flash chip 0\n";
            return false;
        }

        //Clear whatever was at bitstream guard location
        if(!sectorErase(Addr, COMMAND_4KB_SUBSECTOR_ERASE))
            return false;

        //We skip the first page of 4KB subsector so that there's a page of dummy words before bitstream guard
        memcpy(write_buffer, buf2, sizeof(buf)/2);
        if(!writePage(Addr+WRITE_DATA_SIZE)) {
            std::cout << "ERROR: Unable to write bitstream guard to flash chip 0!\n";
            return false;
    }

    return true;
    }
}

bool XSPI_Flasher::clearBitstreamGuard(unsigned int Addr) {
    //Clear whatever was at bitstream guard location
    if(!isDualQSPI(mDev.get())) {
        if (!prepareXSpi(0)) {
            std::cout << "ERROR: Unable to prepare the flash chip 0\n";
            return false;
        }
        //Clear whatever was at bitstream guard location
        return sectorErase(Addr, COMMAND_4KB_SUBSECTOR_ERASE);
    } else {
        //Select flash chip 0
        if (!prepareXSpi(0)) {
            std::cout << "ERROR: Unable to prepare the flash chip 0\n";
            return false;
        }
        //Clear whatever was at bitstream guard location
        if(!sectorErase(Addr, COMMAND_4KB_SUBSECTOR_ERASE))
            return false;
        //Select flash chip 1
        if (!prepareXSpi(1)) {
            std::cout << "ERROR: Unable to prepare the flash chip 0\n";
            return false;
        }
        //Clear whatever was at bitstream guard location
        return sectorErase(Addr, COMMAND_4KB_SUBSECTOR_ERASE);
    }
}

bool XSPI_Flasher::writeEnable() {
    uint32_t StatusReg = XSpi_GetStatusReg();
    if(StatusReg & XSP_SR_TX_FULL_MASK) {
        std::cout << "Tx fifo fill during WriteEnable" << std::endl;
        return false;
    }

    //1
    uint32_t ControlReg = XSpi_GetControlReg();
    ControlReg |= CONTROL_REG_START_STATE;
    XSpi_SetControlReg(ControlReg);

    //2
    WriteBuffer[BYTE1] = COMMAND_WRITE_ENABLE; //0x06

    if(!finalTransfer(WriteBuffer, NULL, WRITE_ENABLE_BYTES))
        return false;

    return waitTxEmpty();
}

bool XSPI_Flasher::getFlashId()
{
    if(!isFlashReady()) {
        std::cout << "Unable to get flash ready " << std::endl;
        return false;
    }

    bool Status = false;
    /* * Prepare the Write Buffer. */
    WriteBuffer[BYTE1] = COMMAND_IDCODE_READ;

    //First read is throwaway
    Status = finalTransfer(WriteBuffer, ReadBuffer, IDCODE_READ_BYTES);
    if( !Status ) {
        return false;
    }

    Status = finalTransfer(WriteBuffer, ReadBuffer, IDCODE_READ_BYTES);
    if( !Status ) {
        return false;
    }

#if defined(_debug)
    for (int i = 0; i < IDCODE_READ_BYTES; i++)
        std::cout << "Idcode byte[" << i << "] " << std::hex << (int)ReadBuffer[i] << std::endl;
#endif

    //Update flash vendor
    for (size_t i = 0; i < flashVendors.size(); i++)
        if(ReadBuffer[1] == flashVendors[i])
            flashVendor = flashVendors[i];

    //Update max number of sector. Value of 0x18 is 1 128Mbit sector
    //Note that macronix/micron use different #s
    if(ReadBuffer[3] == 0xFF)
        return false;
    else {
        switch(ReadBuffer[3]) {
        case 0x38:
        case 0x17:
        case 0x18:
            MAX_NUM_SECTORS = 1;
            break;
        case 0x39:
        case 0x19:
            MAX_NUM_SECTORS = 2;
            break;
        case 0x3A:
        case 0x20:
            MAX_NUM_SECTORS = 4;
            break;
        case 0x3B:
        case 0x21:
            MAX_NUM_SECTORS = 8;
            break;
        case 0x3C:
        case 0x22:
            MAX_NUM_SECTORS = 16;
            break;
        default:
            std::cout << "ERROR: Unrecognized sector field! Exiting..." << std::endl;
            return false;
        }
    }

    for (int i = 0; i < IDCODE_READ_BYTES; i++)
        ReadBuffer[i] = 0;

    unsigned int ffCount = 0;
    for (int i = 1; i < IDCODE_READ_BYTES; i++) {
        if ((unsigned int)ReadBuffer[i] == 0xff)
            ffCount++;
    }

    if(ffCount == IDCODE_READ_BYTES -1)
        return false;

    return true;
}


bool XSPI_Flasher::finalTransfer(uint8_t *SendBufPtr, uint8_t *RecvBufPtr, int ByteCount)
{
    uint32_t ControlReg;
    uint32_t StatusReg;
    uint32_t Data = 0;
    uint8_t  DataWidth = 8;
    uint32_t SlaveSelectMask = SLAVE_SELECT_MASK;

    uint32_t SlaveSelectReg = 0;
    if(slave_index == 0)
      SlaveSelectReg = static_cast<uint32_t>(~0x01);
    else if(slave_index == 1)
      SlaveSelectReg = static_cast<uint32_t>(~0x02);

    /*
   * Enter a critical section from here to the end of the function since
   * state is modified, an interrupt is enabled, and the control register
   * is modified (r/m/w).
   */
    ControlReg = XSpi_GetControlReg();
    StatusReg = XSpi_GetStatusReg();

    if(TEST_MODE)
        std::cout << "Control/Status " << std::hex << ControlReg << "/" << StatusReg << std::dec << std::endl;


    /*
   * If configured as a master, be sure there is a slave select bit set
   * in the slave select register. If no slaves have been selected, the
   * value of the register will equal the mask.  When the device is in
   * loopback mode, however, no slave selects need be set.
   */
    if (ControlReg & XSP_CR_MASTER_MODE_MASK) {
        if ((ControlReg & XSP_CR_LOOPBACK_MASK) == 0) {
            if (SlaveSelectReg == SlaveSelectMask) {
                std::cout << "No slave selected" << std::endl;
                return false;
            }
        }
    }

    /*
    * Set up buffer pointers.
    */
    uint8_t* SendBufferPtr = SendBufPtr;
    uint8_t* RecvBufferPtr = RecvBufPtr;

    int RemainingBytes = ByteCount;
    unsigned int BytesTransferred = 0;

    /*
    * Fill the DTR/FIFO with as many bytes as it will take (or as many as
    * we have to send). We use the tx full status bit to know if the device
    * can take more data. By doing this, the driver does not need to know
    * the size of the FIFO or that there even is a FIFO. The downside is
    * that the status register must be read each loop iteration.
    */
    StatusReg = XSpi_GetStatusReg();
    if((StatusReg & (1<<10)) != 0) {
        std::cout << "status reg in error situation " << std::endl;
        return false;
    }

    while (((StatusReg & XSP_SR_TX_FULL_MASK) == 0) && (RemainingBytes > 0)) {
        if (DataWidth == 8) {
            Data = *SendBufferPtr;
        } else if (DataWidth == 16) {
            Data = *(uint16_t *)SendBufferPtr;
        } else if (DataWidth == 32){
            Data = *(uint32_t *)SendBufferPtr;
        }

        if (writeReg(XSP_DTR_OFFSET, Data) != 0) {
            return false;
        }
        SendBufferPtr += (DataWidth >> 3);
        RemainingBytes -= (DataWidth >> 3);
        StatusReg = XSpi_GetStatusReg();
        if((StatusReg & (1<<10)) != 0) {
            std::cout << "Write command caused created error" << std::endl;
            return false;
        }
    }


    /*
    * Set the slave select register to select the device on the SPI before
    * starting the transfer of data.
    */
    XSpi_SetSlaveSelectReg(SlaveSelectReg);

    ControlReg = XSpi_GetControlReg();
    StatusReg = XSpi_GetStatusReg();

    if(TEST_MODE)
        std::cout << "Control/Status " << std::hex << ControlReg << "/" << StatusReg << std::dec << std::endl;

    if((StatusReg & (1<<10)) != 0) {
        std::cout << "status reg in error situation: 2 " << std::endl;
        return false;
    }

    /*
    * Start the transfer by no longer inhibiting the transmitter and
    * enabling the device. For a master, this will in fact start the
    * transfer, but for a slave it only prepares the device for a transfer
    * that must be initiated by a master.
    */
    ControlReg = XSpi_GetControlReg();
    ControlReg &= ~XSP_CR_TRANS_INHIBIT_MASK;
    XSpi_SetControlReg(ControlReg);

    if(TEST_MODE)
        std::cout << "Control/Status " << std::hex << ControlReg << "/" << StatusReg << std::dec << std::endl;


    //Data transfer to actual flash has already started happening here.

    { /* Polled mode of operation */

        // poll the status register to * Transmit/Receive SPI data.
        while(ByteCount > 0)
        {

            /*
             * Wait for the transfer to be done by polling the
             * Transmit empty status bit
             */
            do {
                StatusReg = XSpi_GetStatusReg();
            } while ((StatusReg & XSP_SR_TX_EMPTY_MASK) == 0);

            /*
             * A transmit has just completed. Process received data
             * and check for more data to transmit. Always inhibit
             * the transmitter while the transmit register/FIFO is
             * being filled, or make sure it is stopped if we're
             * done.
             */
            ControlReg = XSpi_GetControlReg();
            XSpi_SetControlReg(ControlReg | XSP_CR_TRANS_INHIBIT_MASK);

            ControlReg = XSpi_GetControlReg();

            if(TEST_MODE)
                std::cout << "Control/Status " << std::hex << ControlReg << "/" << StatusReg << std::dec << std::endl;

            /*
             * First get the data received as a result of the
             * transmit that just completed. We get all the data
             * available by reading the status register to determine
             * when the Receive register/FIFO is empty. Always get
             * the received data, but only fill the receive
             * buffer if it points to something (the upper layer
             * software may not care to receive data).
             */
            StatusReg = XSpi_GetStatusReg();

            while ((StatusReg & XSP_SR_RX_EMPTY_MASK) == 0)
            {
                //read the data.
                try {
                    Data = readReg(XSP_DRR_OFFSET);
                } catch (const std::exception&) {
                    return false;
                }

                if (DataWidth == 8) {
                    if(RecvBufferPtr != NULL) {
                        *RecvBufferPtr++ = (uint8_t)Data;
                    }
                } else if (DataWidth == 16) {
                    if (RecvBufferPtr != NULL){
                        *(uint16_t *)RecvBufferPtr = (uint16_t)Data;
                        RecvBufferPtr += 2;
                    }
                } else if (DataWidth == 32) {
                    if (RecvBufferPtr != NULL){
                        *(uint32_t *)RecvBufferPtr = Data;
                        RecvBufferPtr += 4;
                    }
                }

                BytesTransferred += (DataWidth >> 3);
                ByteCount -= (DataWidth >> 3);
                StatusReg = XSpi_GetStatusReg();
                if((StatusReg & (1<<10)) != 0) {
                    std::cout << "status reg in error situation " << std::endl;
                    return false;
                }
            }

            //If there are still unwritten bytes, then finishing writing (below code)
            //and reading (above code) them.
            if (RemainingBytes > 0) {
                /*
                 * Fill the DTR/FIFO with as many bytes as it
                 * will take (or as many as we have to send).
                 * We use the Tx full status bit to know if the
                 * device can take more data.
                 * By doing this, the driver does not need to
                 * know the size of the FIFO or that there even
                 * is a FIFO.
                 * The downside is that the status must be read
                 * each loop iteration.
                 */
                StatusReg = XSpi_GetStatusReg();

                while(((StatusReg & XSP_SR_TX_FULL_MASK)== 0) && (RemainingBytes > 0))
                {
                    if (DataWidth == 8) {
                        Data = *SendBufferPtr;
                    } else if (DataWidth == 16) {
                        Data = *(uint16_t *)SendBufferPtr;
                    } else if (DataWidth == 32) {
                        Data = *(uint32_t *)SendBufferPtr;
                    }

                    if(writeReg(XSP_DTR_OFFSET, Data) != 0) {
                        return false;
                    }

                    SendBufferPtr += (DataWidth >> 3);
                    RemainingBytes -= (DataWidth >> 3);
                    StatusReg = XSpi_GetStatusReg();
                    if((StatusReg & (1<<10)) != 0) {
                        std::cout << "status reg in error situation " << std::endl;
                        return false;
                    }
                }

                //Start the transfer by not inhibiting the transmitter any longer.
                ControlReg = XSpi_GetControlReg();
                ControlReg &= ~XSP_CR_TRANS_INHIBIT_MASK;
                XSpi_SetControlReg(ControlReg);
            }
        }

        //Stop the transfer by inhibiting * the transmitter.
        ControlReg = XSpi_GetControlReg();
        XSpi_SetControlReg(ControlReg | XSP_CR_TRANS_INHIBIT_MASK);

        /*
         * Deassert the slaves on the SPI bus when the transfer is complete,
         */
        XSpi_SetSlaveSelectReg(SlaveSelectMask);
    }

    return true;
}


bool XSPI_Flasher::writePage(unsigned int Addr, uint8_t writeCmd)
{
    if(!isFlashReady())
        return false;

    if(!FOUR_BYTE_ADDRESSING) {
        //Select sector when only using 24bit address
        if(!setSector(Addr)) {
            std::cout << "ERROR: Unable to set sector for writePage cmd" << std::endl;
            return false;
        }
    }

    if(!writeEnable())
        return false;

    //1 : reset Tx and Rx FIFO's
    uint32_t ControlReg = CONTROL_REG_START_STATE;
    //  uint32_t ControlReg = XSpi_GetControlReg();
    //  ControlReg |= XSP_CR_RXFIFO_RESET_MASK ;
    //  ControlReg |= XSP_CR_TXFIFO_RESET_MASK;
    XSpi_SetControlReg(ControlReg);

    uint8_t WriteCmd = writeCmd;
    //2
    if(!FOUR_BYTE_ADDRESSING) {
        //3 byte address mode
        //COMMAND_PAGE_PROGRAM gives out all FF's
        //COMMAND_EXT_QUAD_WRITE: hangs the system
        if(writeCmd == 0xff) {
            if(flashVendor == MACRONIX_VENDOR_ID)
                WriteCmd = COMMAND_PAGE_PROGRAM;
            else
                WriteCmd = COMMAND_QUAD_WRITE;
        }

        WriteBuffer[BYTE1] = WriteCmd;
        WriteBuffer[BYTE2] = (uint8_t) (Addr >> 16);
        WriteBuffer[BYTE3] = (uint8_t) (Addr >> 8);
        WriteBuffer[BYTE4] = (uint8_t) Addr;
    }else {
        if(writeCmd == 0xff)
            WriteBuffer[BYTE1] = FOUR_BYTE_QUAD_INPUT_FAST_PROGRAM;
        WriteBuffer[BYTE2] = (uint8_t) (Addr >> 24);
        WriteBuffer[BYTE3] = (uint8_t) (Addr >> 16);
        WriteBuffer[BYTE4] = (uint8_t) (Addr >> 8);
        WriteBuffer[BYTE5] = (uint8_t) Addr;
    }

    //The data to write is already filled up, so now just write the buffer.
    if(!finalTransfer(WriteBuffer, ReadBuffer, WRITE_DATA_SIZE + READ_WRITE_EXTRA_BYTES))
        return false;

    if(!waitTxEmpty())
        return false;


    return true;

}

bool XSPI_Flasher::readPage(unsigned int Addr, uint8_t readCmd)
{
    if(!isFlashReady())
        return false;

    if(!FOUR_BYTE_ADDRESSING) {
        //Select sector when only using 24bit address
        if(!setSector(Addr)) {
            std::cout << "ERROR: Unable to set sector for writePage cmd" << std::endl;
            return false;
        }
    }

    //--
    uint32_t ControlReg = CONTROL_REG_START_STATE;
    //  uint32_t ControlReg = XSpi_GetControlReg();
    //  ControlReg |= XSP_CR_RXFIFO_RESET_MASK ;
    //  ControlReg |= XSP_CR_TXFIFO_RESET_MASK;
    XSpi_SetControlReg(ControlReg);

    //1 : reset TX/RX FIFO's
    uint8_t ReadCmd = readCmd;

    //uint8_t ReadCmd = COMMAND_RANDOM_READ;
    if(!FOUR_BYTE_ADDRESSING) {
        //3 byte addressing mode
        if(readCmd == 0xff)
            ReadCmd = COMMAND_QUAD_READ;

        //3 byte address mode
        WriteBuffer[BYTE1] = ReadCmd;
        WriteBuffer[BYTE2] = (uint8_t) (Addr >> 16);
        WriteBuffer[BYTE3] = (uint8_t) (Addr >> 8);
        WriteBuffer[BYTE4] = (uint8_t) Addr;
    }else {
        if(readCmd == 0xff)
            ReadCmd = FOUR_BYTE_READ;
        WriteBuffer[BYTE1] = ReadCmd;
        WriteBuffer[BYTE2] = (uint8_t) (Addr >> 24);
        WriteBuffer[BYTE3] = (uint8_t) (Addr >> 16);
        WriteBuffer[BYTE4] = (uint8_t) (Addr >> 8);
        WriteBuffer[BYTE5] = (uint8_t) Addr;
    }

    unsigned int ByteCount = READ_DATA_SIZE;

    if (ReadCmd == COMMAND_DUAL_READ) {
        ByteCount += DUAL_READ_DUMMY_BYTES;
    } else if (ReadCmd == COMMAND_DUAL_IO_READ) {
        ByteCount += DUAL_READ_DUMMY_BYTES;
    } else if (ReadCmd == COMMAND_QUAD_IO_READ) {
        ByteCount += QUAD_IO_READ_DUMMY_BYTES;
    } else if ( (ReadCmd==COMMAND_QUAD_READ) || (ReadCmd==FOUR_BYTE_QUAD_OUTPUT_FAST_READ)) {
        ByteCount += QUAD_READ_DUMMY_BYTES;
    }

    if(!finalTransfer(WriteBuffer, ReadBuffer, ByteCount + READ_WRITE_EXTRA_BYTES))
        return false;

    if(!waitTxEmpty())
        return false;

    //reset the RXFIFO bit so.
    ControlReg = XSpi_GetControlReg();
    ControlReg |= XSP_CR_RXFIFO_RESET_MASK ;
    XSpi_SetControlReg(ControlReg);

    return true;

}

bool XSPI_Flasher::prepareXSpi(uint8_t slave_sel)
{
    if(TEST_MODE)
        return true;

    //Set slave index
    slave_index = slave_sel;
#if defined(_debug)
    std::cout << "Slave select " << slave_sel  << std::endl;
#endif

    //Resetting selected_sector
	selected_sector = std::numeric_limits<uint32_t>::max();

    XSPI_UNUSED uint32_t tControlReg = XSpi_GetControlReg();
    XSPI_UNUSED uint32_t tStatusReg = XSpi_GetStatusReg();

#if defined(_debug)
  std::cout << "Boot Control/Status " << std::hex << tControlReg << "/" << tStatusReg << std::dec << std::endl;
#endif

    //Reset IP
    XSpi_WriteReg(XSP_SRR_OFFSET, XSP_SRR_RESET_MASK);

    //Init IP settings
    uint32_t ControlReg = CONTROL_REG_START_STATE;
    XSpi_SetControlReg(ControlReg);

    tControlReg = XSpi_GetControlReg();
    tStatusReg = XSpi_GetStatusReg();

#if defined(_debug)
  std::cout << "After setting start state, Control/Status " << std::hex << tControlReg << "/" << tStatusReg << std::dec << std::endl;
#endif
    //--

    if(!getFlashId()) {
        std::cout << "Exiting now, as could not get correct idcode" << std::endl;
        exit(-EOPNOTSUPP);
    }

#if defined(_debug)
    std::cout << "Slave " << slave_sel << " ready" << std::endl;
#endif

    delay(std::chrono::microseconds(20));

    return true;
}

int XSPI_Flasher::programRecord(std::istream& mcsStream, const ELARecord& record) {

#if defined(_debug)
    std::cout << "Programming block (" << std::hex << record.mStartAddress << ", " << record.mEndAddress << std::dec << ")" << std::endl;
#endif

    assert(mcsStream.tellg() < record.mDataPos);
    mcsStream.seekg(record.mDataPos, std::ios_base::beg);
    unsigned char* buffer = &WriteBuffer[READ_WRITE_EXTRA_BYTES];
    int bufferIndex = 0;
    int pageIndex = 0;
    std::string prevLine("");
    for (unsigned int index = record.mDataCount; index > 0;) {
        std::string line;
        std::getline(mcsStream, line);
        if(TEST_MODE)
            std::cout << line << std::endl;
        const unsigned int dataLen = std::stoi(line.substr(1, 2), 0 , 16);
        index -= dataLen;
        const unsigned int recordType = std::stoi(line.substr(7, 2), 0 , 16);
        if (recordType != 0x00) {
            continue;
        }
        const std::string data = line.substr(9, dataLen * 2);
        // Write in byte swapped order
        for (unsigned int i = 0; i < data.length(); i += 2) {
            unsigned int value = std::stoi(data.substr(i, 2), 0, 16);
            buffer[bufferIndex++] = (unsigned char)value;
            assert(bufferIndex <= WRITE_DATA_SIZE);

#if 0
            //To enable byte swapping uncomment this.
            //      if ((bufferIndex % 4) == 0) {
            //        bufferIndex += 4;
            //      }
            //      assert(bufferIndex <= WRITE_DATA_SIZE);
            //      unsigned value = std::stoi(data.substr(i, 2), 0, 16);
            //      if(TEST_MODE)
            //        std::cout << data.substr(i, 2);
            //      buffer[--bufferIndex] = (unsigned char)value;
            //      if ((bufferIndex % 4) == 0) {
            //        bufferIndex += 4;
            //      }
#endif
            if (bufferIndex == WRITE_DATA_SIZE) {
                break;
            }
        }

        if(TEST_MODE)
            std::cout << std::endl;

#if 0
        //Uncomment if byte swapping enabled.

        //account for the last line
        //which can have say 14 bytes instead of 16
        if((bufferIndex %4)!= 0) {
            while ((bufferIndex %4)!= 0) {
                unsigned char fillValue = 0xFF;
                buffer[--bufferIndex] = fillValue;
            }
            bufferIndex += 4;
        }

        assert((bufferIndex % 4) == 0);
#endif

        assert(bufferIndex <= WRITE_DATA_SIZE);
        if (bufferIndex == WRITE_DATA_SIZE) {
#if defined(_debug)
            std::cout << "writing page " << pageIndex << std::endl;
#endif
            const unsigned int address = std::stoi(line.substr(3, 4), 0, 16);
            if(TEST_MODE) {
                std::cout << (address + dataLen) << " " << (pageIndex +1)*WRITE_DATA_SIZE << std::endl;
                std::cout << record.mStartAddress << " " << record.mStartAddress + pageIndex*PAGE_SIZE;
                std::cout << " " << address << std::endl;
            } else {
                if(!writePage(record.mStartAddress + pageIndex*WRITE_DATA_SIZE))
                    return -ENXIO;
                clearBuffers();
                {
                    //debug stuff
#if defined(_debug)
                    if(pageIndex == 0) {
                        if(!readPage(record.mStartAddress + pageIndex*WRITE_DATA_SIZE))
                            return -ENXIO;
                        clearBuffers();
                    }
#endif
                }
            }
            pageIndex++;
            delay(std::chrono::microseconds(20));
            bufferIndex = 0;
        }
        prevLine = line;

    }
    if (bufferIndex) {
        //Write the last page
        if(TEST_MODE) {
            std::cout << "writing final page " << pageIndex << std::endl;
            std::cout << bufferIndex << std::endl;
            std::cout << prevLine << std::endl;
        }

        const unsigned int address = std::stoi(prevLine.substr(3, 4), 0, 16);
        const unsigned int dataLen = std::stoi(prevLine.substr(1, 2), 0 , 16);

        if(TEST_MODE)
            std::cout << address % WRITE_DATA_SIZE << " " << dataLen << std::endl;

        //assert( (address % WRITE_DATA_SIZE + dataLen) == bufferIndex);

        if(!TEST_MODE) {

            //Fill unused half page to FF
            for(unsigned int i = bufferIndex; i < WRITE_DATA_SIZE; ++i) {
                buffer[i] = 0xff;
            }

            if(!writePage(record.mStartAddress + pageIndex*WRITE_DATA_SIZE))
                return -ENXIO;
            delay(std::chrono::microseconds(20));
            clearBuffers();
            {
                //debug stuff
#if defined(_debug)
                if(!readPage(record.mStartAddress + pageIndex*WRITE_DATA_SIZE))
                    return -ENXIO;
                clearBuffers();
#endif
            }
        }
    }
    return 0;
}

int XSPI_Flasher::programXSpi(std::istream& mcsStream, uint32_t bitstream_shift_addr)
{

    //Now we can safely erase all subsectors
    int beatCount = 0;
    XBU::ProgressBar erase_flash("Erasing flash", static_cast<unsigned int>(recordList.size()), XBU::is_esc_enabled(), std::cout);
    for (ELARecordList::iterator i = recordList.begin(), e = recordList.end(); i != e; ++i) {
        beatCount++;
        // if(beatCount%20==0) {
            erase_flash.update(beatCount);
        // }

        //Shift all write addresses below bitstream guard
        i->mStartAddress += bitstream_shift_addr;
        i->mEndAddress += bitstream_shift_addr;

        //Erase any subsectors in address range.
        for(uint32_t j = i->mStartAddress; j < i->mEndAddress; j+=0x1000) {
            //std::cout << "DEBUG: Erasing subsector @ 0x" << std::hex << j << std::dec << std::endl;
            if(!sectorErase(j, COMMAND_4KB_SUBSECTOR_ERASE)) {
                // std::cout << "\nERROR: Failed to erase subsector!" << std::endl;
                erase_flash.finish(false, "Failed to erase subsector!");
                return -EINVAL;
            }

            delay(std::chrono::microseconds(20));
        }
    }
    erase_flash.finish(true, "Flash erased");

    //Next we program flash. Note that bitstream guard is still active
    beatCount = 0;
    XBU::ProgressBar program_flash("Programming flash", static_cast<unsigned int>(recordList.size()), XBU::is_esc_enabled(), std::cout);
    for (ELARecordList::iterator i = recordList.begin(), e = recordList.end(); i != e; ++i)
    {
        beatCount++;
        // if(beatCount%20==0) {
            program_flash.update(beatCount);
        // }

        if(TEST_MODE) {
            std::cout << boost::format("%-8s : %s %x\n") % "INFO" % "Start address 0x" % recordList.front().mStartAddress;
            std::cout << boost::format("%-8s : %s %x\n") % "INFO" % "End address 0x" % recordList.front().mEndAddress;
        }

        bool ready = isFlashReady();
        if(!ready){
            program_flash.finish(false, "Unable to get flash ready");
            return -EINVAL;
        }

        clearBuffers();

        if (programRecord(mcsStream, *i)) {
            program_flash.finish(false, "Could not program the block");
            return -EINVAL;
        }

        delay(std::chrono::microseconds(20));
    }
    program_flash.finish(true, "Flash programmed");
    return 0;
}

bool XSPI_Flasher::readRegister(uint8_t commandCode, unsigned int bytes) {

    if(!isFlashReady())
        return false;

    bool Status = false;

    WriteBuffer[BYTE1] = commandCode;

    Status = finalTransfer(WriteBuffer, ReadBuffer, bytes);

    if( !Status ) {
        return false;
    }

#if defined(_debug)
    std::cout << "Printing output (with some extra bytes of readRegister cmd)" << std::endl;
#endif

    for(unsigned int i = 0; i < 5; ++ i) //Some extra bytes, no harm
    {
#if defined(_debug)
        std::cout << i << " " << std::hex << (int)ReadBuffer[i] << std::dec << std::endl;
#endif
        ReadBuffer[i] = 0; //clear
    }
    //Reset the FIFO bit.
    uint32_t ControlReg = XSpi_GetControlReg();
    ControlReg |= XSP_CR_RXFIFO_RESET_MASK ;
    ControlReg |= XSP_CR_TXFIFO_RESET_MASK ;
    XSpi_SetControlReg(ControlReg);

    return Status;
}

//max 16 bits for nonvolative cfg register.
//If extra_bytes == 0, then only the command is sent.
bool XSPI_Flasher::writeRegister(uint8_t commandCode, unsigned int value, unsigned int extra_bytes) {
    if(!isFlashReady())
        return false;

    if(!writeEnable())
        return false;

    uint32_t ControlReg = XSpi_GetControlReg();
    ControlReg |= XSP_CR_TXFIFO_RESET_MASK;
    ControlReg |= XSP_CR_RXFIFO_RESET_MASK;
    XSpi_SetControlReg(ControlReg);

    bool Status = false;

    WriteBuffer[BYTE1] = commandCode;

    if(extra_bytes == 0) {
        //do nothing
    } else if(extra_bytes == 1)
        WriteBuffer[BYTE2] = (uint8_t) (value);
    else if(extra_bytes == 2) {
        WriteBuffer[BYTE2] = (uint8_t) (value >> 8);
        WriteBuffer[BYTE3] = (uint8_t) value;
    }else {
        std::cout << "ERROR: Setting more than 2 bytes" << std::endl;
        assert(0);
    }

    //+1 for cmd byte.
    Status = finalTransfer(WriteBuffer,NULL, extra_bytes+1);
    if(!Status)
        return false;

    if(!waitTxEmpty())
        return false;

    return Status;
}

// The bitstream guard location is fixed for now for all platforms.
const unsigned int dftBitstreamGuardAddress = 0x01002000;
const unsigned int bitstreamGuardSize = 4096;
// Print out "." for each pagesz bytes of data processed.
const size_t pagesz = 1024 * 1024ul;

static inline long toAddr(const int slave, const unsigned int offset)
{
    long addr = slave;

    // Slave index is the MSB of the address.
    addr <<= 56;
    addr |= offset;
    return addr;
}

static int writeToFlash(std::FILE *flashDev, int slave,
    const unsigned int address, const unsigned char *buf, size_t len)
{
    int ret = 0;
    long addr = toAddr(slave, address);

    ret = std::fseek(flashDev, addr, SEEK_SET);
    if (ret)
        return ret;

    std::fwrite(buf, 1, len, flashDev);
    std::fflush(flashDev);
    if (ferror(flashDev))
        ret = -errno;

    return ret;
}

static int installBitstreamGuard(xrt_core::device *dev, std::FILE *flashDev,
    uint32_t address)
{
    const size_t bitstream_guard_offset = 128;
    unsigned char buf[4096], buf1[4096], buf2[4096];
    int ret;

    memset(buf, 0xff, sizeof(buf));
    memset(buf, 0xff, sizeof(buf1));
    memset(buf, 0xff, sizeof(buf2));
    memcpy(buf + bitstream_guard_offset, BITSTREAM_GUARD,
        sizeof(BITSTREAM_GUARD));
    stripe_data(buf, buf1, buf2, sizeof(buf));

    if(!isDualQSPI(dev)) {
        ret = writeToFlash(flashDev, 0, address, buf, sizeof(buf));
        if (ret) {
            std::cout << "Failed to install bitstream guard: "
                << ret << std::endl;
        } else {
            std::cout << "Bitstream guard installed on flash @0x"
                << std::hex << address << std::dec << std::endl;
        }
    } else {
        ret = writeToFlash(flashDev, 0, address, buf1, sizeof(buf1));
        if (ret) {
            std::cout << "Failed to install bitstream guard on flash 0: "
                << ret << std::endl;
            return ret;
        }
        ret = writeToFlash(flashDev, 1, address, buf2, sizeof(buf2));
        if (ret) {
            std::cout << "Failed to install bitstream guard on flash 1: "
                << ret << std::endl;
        } else {
            std::cout << "Bitstream guard installed on both flashes @0x"
                << std::hex << address << std::dec << std::endl;
        }
    }

    return ret;
}

static int removeBitstreamGuard(xrt_core::device *dev, std::FILE *flashDev,
    uint32_t address)
{
    unsigned char buf[4096];
    int ret;

    memset(buf, 0xff, sizeof(buf));

    if(!isDualQSPI(dev)) {
        ret = writeToFlash(flashDev, 0, address, buf, sizeof(buf));
        if (ret) {
            std::cout << "Failed to remove bitstream guard from flash: "
                << ret << std::endl;
        } else {
            std::cout << "Bitstream guard removed from flash" << std::endl;
        }
    } else {
        ret = writeToFlash(flashDev, 0, address, buf, sizeof(buf)/2);
        if (ret) {
            std::cout << "Failed to remove bitstream guard from flash 0: "
                << ret << std::endl;
        } else {
            ret = writeToFlash(flashDev, 1, address, buf, sizeof(buf)/2);
            if (ret) {
                std::cout << "Failed to remove bitstream guard from flash 1: "
                    << ret << std::endl;
            } else {
                std::cout << "Bitstream guard removed from both flashes"
                << std::endl;
            }
        }
    }
    return ret;
}

static int bitstreamGuardAddress(xrt_core::device *dev, uint32_t& addr)
{
    // Shift bitstream guard address if dual QSPI
    if (isDualQSPI(dev))
        addr = dftBitstreamGuardAddress >> 1;
    else
        addr = dftBitstreamGuardAddress;
    return 0;
}

int XSPI_Flasher::revertToMFG(void)
{
    // Vendor-dependent behaviour
    auto vendor = xrt_core::device_query<xrt_core::query::pcie_vendor>(mDev);
    switch (vendor) {
        case ARISTA_ID:
            return fullErase() ? 0 : -EINVAL;
        default:
        case XILINX_ID:
            break;
    }

    uint32_t bitstream_start_loc;

    int ret = bitstreamGuardAddress(mDev.get(), bitstream_start_loc);
    if (ret)
        return ret;

    if (mFlashDev)
        return installBitstreamGuard(mDev.get(), mFlashDev, bitstream_start_loc);

    if(!writeBitstreamGuard(bitstream_start_loc)) {
        std::cout << "ERROR: Unable to set bitstream guard!" << std::endl;
        return -EINVAL;
    }
    return 0;
}

static int splitMcsLine(std::string& line,
    unsigned int& type, unsigned int& address, std::vector<unsigned char>& data)
{
    int ret = 0;

    // Every line should start with ":"
    if (line[0] != ':')
        return -EINVAL;

    unsigned int len = std::stoi(line.substr(1, 2), NULL , 16);
    type = std::stoi(line.substr(7, 2), NULL , 16);
    address = std::stoi(line.substr(3, 4), NULL, 16);
    data.clear();

    switch (type) {
    case 0:
        if (len > 16) {
            // For xilinx mcs files data length should be 16 for all records
            // except for the last one which can be smaller
            ret = -EINVAL;
        } else {
            unsigned int l = len * 2;
            std::string d = line.substr(9, len * 2);
            for (unsigned int i = 0; i < l; i += 2)
                data.push_back(static_cast<unsigned char>(std::stoi(d.substr(i, 2), NULL, 16)));
        }
        break;
    case 1:
        break;
    case 4:
        if (len != 2) {
            // For xilinx mcs files extended address can only be 2 bytes
            ret = -EINVAL;
        }
        address = std::stoi(line.substr(9, len * 2), NULL, 16);
        break;
    default:
        // Xilinx mcs files should not contain other types
        ret = -EINVAL;
        break;
    }

    return ret;
}

static inline unsigned int pageOffset(unsigned int addr)
{
    return (addr & 0xffff);
}

static bool mcsStreamIsGolden(std::istream& mcsStream)
{
    std::string line;
    unsigned int type = 0;
    unsigned int addr = 0;
    std::vector<unsigned char> data;

    mcsStream.seekg(0, std::ios_base::beg);

    // Try to find the first address in mcs stream
    while (!mcsStream.eof() && type != 4) {
        std::getline(mcsStream, line);
        if (!line.empty())
            splitMcsLine(line, type, addr, data);
    }

    mcsStream.seekg(0, std::ios_base::beg);
    return addr == 0;
}

static int mcsStreamToBin(std::istream& mcsStream, unsigned int& currentAddr,
    std::vector<unsigned char>& buf, unsigned int& nextAddr)
{
    bool done = false;
    size_t cnt = 0;

    buf.clear();

    while (!mcsStream.eof() && !done) {
        std::string line;
        unsigned int type;
        unsigned int addr;
        std::vector<unsigned char> data;

        std::getline(mcsStream, line);
        if (line.size() == 0)
            continue;

        if (splitMcsLine(line, type, addr, data) != 0) {
            std::cout << "Found invalid MCS line: " << line << std::endl;
            return -EINVAL;
        }

        switch (type) {
        case 0:
            if (currentAddr == UINT_MAX) {
                std::cout << "MCS missing page starting address" << std::endl;
                return -EINVAL;
            } else if (buf.size() == 0) {
                // we're the 1st data in this page, updating the current addr
                assert(pageOffset(currentAddr) == 0);
                currentAddr |= addr;
            } else if (pageOffset(currentAddr + static_cast<unsigned int>(buf.size())) != addr) {
                std::cout << "MCS page offset is not contiguous, expecting 0x"
                    << std::hex << pageOffset(currentAddr) + buf.size()
                    << ", but, got 0x" << addr << std::dec << std::endl;
                return -EINVAL;
            }
            // keep adding data to existing buffer
            buf.insert(buf.end(), data.begin(), data.end());
            cnt += data.size();
            break;
        case 1:
            // end current buffer and no more
            nextAddr = UINT_MAX;
            done = true;
            break;
        case 4:
            addr <<= 16;
            if (currentAddr == UINT_MAX) {
                currentAddr = addr; // addr of the very first page
            } else if (currentAddr + buf.size() != addr) {
                nextAddr = addr; // start new page
                done = true;
            }
            // otherwise, continue to next page since page is still contiguous
            break;
        default:
            assert(1); // shouldn't be here
            break;
        }

        if (cnt >= pagesz) {
            std::cout << "." << std::flush;
            cnt = 0;
        }
    }
    if (cnt) // print the last "."
            std::cout << "." << std::flush;
    std::cout << std::endl;

    if (buf.size() > UINT_MAX) {
        std::cout << "MCS bitstream is too large: 0x" << std::hex << buf.size()
            << std::dec << " bytes" << std::endl;
        return -EINVAL;
    }

    return 0;
}

static int writeBitstream(std::FILE *flashDev, int index, unsigned int addr,
    std::vector<unsigned char>& buf)
{
    int ret = 0;
    size_t len = 0;

    // Write to flash page by page and print '.' for each write
    // as progress indicator
    for (size_t i = 0; ret == 0 && i < buf.size(); i += len) {
        len = pagesz - ((addr + i) % pagesz);
        len = std::min(len, buf.size() - i);

        std::cout << "." << std::flush;
        ret = writeToFlash(flashDev, index, addr + static_cast<unsigned int>(i), buf.data() + static_cast<unsigned int>(i), len);
    }
    std::cout << std::endl;
    return ret;
}

static int programXSpiDrv(std::FILE *mFlashDev, std::istream& mcsStream,
    int index, uint32_t addressShift)
{
    // Parse MCS data and write each contiguous chunk to flash.
    std::vector<unsigned char> buf;
    unsigned int curAddr = UINT_MAX;
    unsigned int nextAddr = 0;
    int ret;

    while (nextAddr != UINT_MAX) {
        std::cout << "Extracting bitstream from MCS data:" << std::endl;
        ret = mcsStreamToBin(mcsStream, curAddr, buf, nextAddr);
        if (ret)
            return ret;
        assert(nextAddr == UINT_MAX || pageOffset(nextAddr) == 0);
        std::cout << "Extracted " << buf.size() << " bytes from bitstream @0x"
            << std::hex << curAddr << std::dec << std::endl;

        std::cout << "Writing bitstream to flash " << index << ":" << std::endl;
        ret = writeBitstream(mFlashDev, index, curAddr + addressShift, buf);
        if (ret)
            return ret;
        curAddr = nextAddr;
    }

    return 0;
}

int XSPI_Flasher::upgradeFirmware1Drv(std::istream& mcsStream)
{
    int ret = 0;
    uint32_t bsGuardAddr;

    if (mcsStreamIsGolden(mcsStream))
        return programXSpiDrv(mFlashDev, mcsStream, 0, 0);

    ret = bitstreamGuardAddress(mDev.get(), bsGuardAddr);
    if (ret)
        return ret;

    // Enable bitstream guard.
    ret = installBitstreamGuard(mDev.get(), mFlashDev, bsGuardAddr);
    if (ret)
        return ret;

    // Write MCS
    ret = programXSpiDrv(mFlashDev, mcsStream, 0, bitstreamGuardSize);
    if (ret)
        return ret;

    // Disable bitstream guard.
    return removeBitstreamGuard(mDev.get(), mFlashDev, bsGuardAddr);
}

int XSPI_Flasher::upgradeFirmware2Drv(std::istream& mcsStream0,
    std::istream& mcsStream1)
{
    int ret = 0;
    uint32_t bsGuardAddr;

    if (mcsStreamIsGolden(mcsStream0)) {
        ret = programXSpiDrv(mFlashDev, mcsStream0, 0, 0);
        if (ret)
            return ret;
        return programXSpiDrv(mFlashDev, mcsStream1, 1, 0);
    }

    ret = bitstreamGuardAddress(mDev.get(), bsGuardAddr);
    if (ret)
        return ret;

    // Enable bitstream guard.
    ret = installBitstreamGuard(mDev.get(), mFlashDev, bsGuardAddr);
    if (ret)
        return ret;

    // Write MCS
    ret = programXSpiDrv(mFlashDev, mcsStream0, 0, bitstreamGuardSize);
    if (ret)
        return ret;
    ret = programXSpiDrv(mFlashDev, mcsStream1, 1, bitstreamGuardSize);
    if (ret)
        return ret;

    // Disable bitstream guard.
    return removeBitstreamGuard(mDev.get(), mFlashDev, bsGuardAddr);
}
