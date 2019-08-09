#include "i2c.h"
#include "i2c_rw.h"
#include "USB_PD_defines.h"
#include "USBPD_CUST_NVM_API.h"

#include "STUSB_NVM.h"  // Add NVM configuration generated by GUI

#define NVM_SIZE 40     // 5 sectors, 8 bytes each

/*
* Write the configuration from STUSB_NVM.h to the STUSB4500's NVM
*/
int nvm_flash() {
    if (CUST_EnterWriteMode(0, SECTOR_0 | SECTOR_1  | SECTOR_2 | SECTOR_3 | SECTOR_4 ) != 0) return -1;
    if (CUST_WriteSector(0, 0, Sector0) != 0) return -1;
    if (CUST_WriteSector(0, 1, Sector1) != 0) return -1;
    if (CUST_WriteSector(0, 2, Sector2) != 0) return -1;
    if (CUST_WriteSector(0, 3, Sector3) != 0) return -1;
    if (CUST_WriteSector(0, 4, Sector4) != 0) return -1;
    if (CUST_ExitTestMode(0) != 0) return -1;
    
    return 0;
}

/*
* Read all 5 x 8 = 40 bytes of the NVM to SectorsOut
*/
int nvm_read(unsigned char* pSectorsOut, int SectorsLength) {
    if(SectorsLength < NVM_SIZE) return -2;

    unsigned char Sectors[5][8];
    unsigned char* pSectors = &Sectors[0][0];
        
    if (CUST_EnterReadMode(0) != 0) return -1;
    if (CUST_ReadSector(0, 0, &Sectors[0][0]) != 0) return -1;
    if (CUST_ReadSector(0, 1, &Sectors[1][0]) != 0) return -1;
    if (CUST_ReadSector(0, 2, &Sectors[2][0]) != 0) return -1;
    if (CUST_ReadSector(0, 3, &Sectors[3][0]) != 0) return -1;
    if (CUST_ReadSector(0, 4, &Sectors[4][0]) != 0) return -1;
    if (CUST_ExitTestMode(0) != 0) return -1;
    
    for (int i = 0; i < NVM_SIZE; i++) {
        pSectorsOut[i] = pSectors[i];
    }
    
    return 0;
}

/* FTP Registers Documentation
    FTP_CUST_PASSWORD REG: address 0x95
        [7:0] : Password required to flash NVM, password = FTP_CUST_PASSWORD = 0x47

    FTP_CTRL_0: address 0x96
        [7]   : FTP_CUST_PWR    : Power
        [6]   : FTP_CUST_RST_N  : Reset
        [5]   : FTP_CUST_REQ    : Not used
        [4]   : FTP_CUST_SECT   : Request operation
        [3]   : --------------  : --------------
        [2:0] : FTP_CUST_SER    : Sector 0 - 4 selection

    FTP_CTRL_1: address 0x97
        [7:3] : FTP_CUST_SER    : Sectors to erase (MSB = sector 4, LSB = sector 0)
        [2:0] : FTP_CUST_OPCODE : Opcode
                000 : Read sector
                001 : Write Program Load register (PL) with data to be written to sector 0 or 1
                010 : Write FTP_CTRL_1[7:3] to Sector Erase register (SER) 
                011 : Read PL
                100 : Read SER
                101 : Erase sectors masked by SER
                110 : Program sector selected by FTP_CTRL_0[2:0]
                111 : Soft program sectors masked by SER

    RW_BUFFER: address 0x53
        [7:0] : Buffer used for reading and writing data
*/

/*
* Set the STUSB4500 to a state where the NVM can be written to
*/
int CUST_EnterWriteMode(uint8_t Port, unsigned char ErasedSector) {
    unsigned char Buffer[2];
    
    // Write FTP_CUST_PASSWORD to FTP_CUST_PASSWORD_REG
    Buffer[0] = FTP_CUST_PASSWORD;
    if (I2C_Write_USB_PD(Port, FTP_CUST_PASSWORD_REG, Buffer, 1) != HAL_OK) return -1;
    
    // RW_BUFFER register must be NULL for Partial Erase feature
    Buffer[0] = 0x00;
    if (I2C_Write_USB_PD(Port, RW_BUFFER, Buffer, 1) != HAL_OK) return -1;
    
    /* Begin NVM power on sequence */
    // Reset internal controller
    Buffer[0] = 0x00;
    if (I2C_Write_USB_PD(Port, FTP_CTRL_0, Buffer, 1) != HAL_OK) return -1;
    
    // Set PWR and RST_N bits in FTP_CTRL_0
    Buffer[0] = FTP_CUST_PWR | FTP_CUST_RST_N;
    if (I2C_Write_USB_PD(Port, FTP_CTRL_0, Buffer, 1) != HAL_OK) return -1;
    /* End NVM power on sequence */
    
    /* Begin erasing specified sectors */
    // Format and mask sectors to erase and write SER write opcode
    Buffer[0] = ((ErasedSector << 3) & FTP_CUST_SER) | (WRITE_SER & FTP_CUST_OPCODE);
    if (I2C_Write_USB_PD(Port, FTP_CTRL_1, Buffer, 1) != HAL_OK) return -1;
    
    // Load SER write command
    Buffer[0] = FTP_CUST_PWR | FTP_CUST_RST_N | FTP_CUST_REQ; 
    if (I2C_Write_USB_PD(Port, FTP_CTRL_0, Buffer, 1) != HAL_OK) return -1;

    // Wait for execution
    do {
        if (I2C_Read_USB_PD(Port, FTP_CTRL_0, Buffer, 1) != HAL_OK) return -1;
    }
    while (Buffer[0] & FTP_CUST_REQ); 
    
    // Write soft program opcode
    Buffer[0] = SOFT_PROG_SECTOR & FTP_CUST_OPCODE;  
    if (I2C_Write_USB_PD(Port, FTP_CTRL_1, Buffer, 1) != HAL_OK) return -1;

    // Load soft program command
    Buffer[0] = FTP_CUST_PWR | FTP_CUST_RST_N | FTP_CUST_REQ; 
    if (I2C_Write_USB_PD(Port, FTP_CTRL_0, Buffer, 1) != HAL_OK) return -1;
    
    // Wait for execution
    do {
        if (I2C_Read_USB_PD(Port, FTP_CTRL_0, Buffer, 1) != HAL_OK) return -1;
    }
    while (Buffer[0] & FTP_CUST_REQ);
    
    // Write erase sectors opcode
    Buffer[0] = ERASE_SECTOR & FTP_CUST_OPCODE;  
    if (I2C_Write_USB_PD(Port, FTP_CTRL_1, Buffer, 1) != HAL_OK) return -1;
    
    // Load erase sectors command
    Buffer[0] = FTP_CUST_PWR | FTP_CUST_RST_N | FTP_CUST_REQ;  
    if (I2C_Write_USB_PD(Port, FTP_CTRL_0, Buffer, 1) != HAL_OK) return -1;
    
    // Wait for execution
    do {
        if (I2C_Read_USB_PD(Port, FTP_CTRL_0, Buffer, 1) != HAL_OK) return -1;
    }
    while (Buffer[0] & FTP_CUST_REQ);
    /* End erasing specified sectors */
    
    return 0;
}

/*
* Set the STUSB4500 to a state where the NVM can be read from
*/
int CUST_EnterReadMode(uint8_t Port) {
    unsigned char Buffer[2];
    
    // Write FTP_CUST_PASSWORD to FTP_CUST_PASSWORD_REG
    Buffer[0] = FTP_CUST_PASSWORD;
    if (I2C_Write_USB_PD(Port, FTP_CUST_PASSWORD_REG, Buffer, 1) != HAL_OK) return -1;
    
    /* Begin NVM power on sequence */
    // Reset internal controller
    Buffer[0] = 0x00;
    if (I2C_Write_USB_PD(Port, FTP_CTRL_0, Buffer, 1) != HAL_OK) return -1;
    
    // Set PWR and RST_N bits in FTP_CTRL_0
    Buffer[0] = FTP_CUST_PWR | FTP_CUST_RST_N;
    if (I2C_Write_USB_PD(Port, FTP_CTRL_0, Buffer, 1) != HAL_OK) return -1;
    /* End NVM power on sequence */
    
    return 0;
}

/*
* Store the byte in the sector indicated by SectorNum to SectorData
*/
int CUST_ReadSector(uint8_t Port, char SectorNum, unsigned char *SectorData) {
    unsigned char Buffer[2];
    
    // Set PWR and RST_N bits in FTP_CTRL_0
    Buffer[0] = FTP_CUST_PWR | FTP_CUST_RST_N;
    if (I2C_Write_USB_PD(Port, FTP_CTRL_0, Buffer, 1) != HAL_OK) return -1;
    
    // Write sector read opcode
    Buffer[0] = (READ & FTP_CUST_OPCODE);
    if (I2C_Write_USB_PD(Port, FTP_CTRL_1, Buffer, 1) != HAL_OK) return -1;
    
    // Select sector to read and load sector read command
    Buffer[0] = (SectorNum & FTP_CUST_SECT) | FTP_CUST_PWR | FTP_CUST_RST_N | FTP_CUST_REQ;
    if (I2C_Write_USB_PD(Port, FTP_CTRL_0, Buffer, 1) != HAL_OK) return -1;
    
    // Wait for execution
    do {
        if (I2C_Read_USB_PD(Port, FTP_CTRL_0, Buffer, 1) != HAL_OK) return -1;
    }
    while (Buffer[0] & FTP_CUST_REQ);
    
    // Read sector data byte from RW_BUFFER register
    if (I2C_Read_USB_PD(Port, RW_BUFFER, &SectorData[0], 8) != HAL_OK) return -1;
    
    // Reset internal controller
    Buffer[0] = 0x00;
    if (I2C_Write_USB_PD(Port, FTP_CTRL_0, Buffer, 1) != HAL_OK) return -1;
    
    return 0;
}

/*
* Write the byte in SectorData to the sector indicated by SectorNum
*/
int CUST_WriteSector(uint8_t Port, char SectorNum, unsigned char *SectorData) {
    unsigned char Buffer[2];
    
    // Write the 8 byte programming data to the RW_BUFFER register
    if (I2C_Write_USB_PD(Port, RW_BUFFER, SectorData, 8) != HAL_OK) return -1;
    
    // Set PWR and RST_N bits in FTP_CTRL_0
    Buffer[0] = FTP_CUST_PWR | FTP_CUST_RST_N;
    if (I2C_Write_USB_PD(Port, FTP_CTRL_0, Buffer, 1) != HAL_OK) return -1;
    
    // Write PL write opcode
    Buffer[0] = (WRITE_PL & FTP_CUST_OPCODE);
    if (I2C_Write_USB_PD(Port, FTP_CTRL_1, Buffer, 1) != HAL_OK) return -1;
    
    // Load PL write command
    Buffer[0] = FTP_CUST_PWR | FTP_CUST_RST_N | FTP_CUST_REQ;
    if (I2C_Write_USB_PD(Port, FTP_CTRL_0, Buffer, 1) != HAL_OK) return -1;
    
    // Wait for execution
    do {
        if (I2C_Read_USB_PD(Port, FTP_CTRL_0, Buffer, 1) != HAL_OK) return -1; /* Wait for execution */
    }		 
    while (Buffer[0] & FTP_CUST_REQ);
    
    // Write program sector opcode
    Buffer[0] = (PROG_SECTOR & FTP_CUST_OPCODE);
    if (I2C_Write_USB_PD(Port, FTP_CTRL_1, Buffer, 1) != HAL_OK) return -1;/*Set Prog Sectors Opcode*/
    
    // Load program sector command
    Buffer[0] = (SectorNum & FTP_CUST_SECT) |FTP_CUST_PWR |FTP_CUST_RST_N | FTP_CUST_REQ;
    if (I2C_Write_USB_PD(Port, FTP_CTRL_0, Buffer, 1) != HAL_OK) return -1; /* Load Prog Sectors Opcode */  
    
    // Wait for execution
    do {
        if (I2C_Read_USB_PD(Port, FTP_CTRL_0, Buffer, 1) != HAL_OK) return -1;
    }
    while(Buffer[0] & FTP_CUST_REQ);

    return 0;
}

/*
* Disable reading/writing the NVM
*/
int CUST_ExitTestMode(uint8_t Port) {
    unsigned char Buffer[2];
    
    // Clear FTP_CTRL registers
    Buffer[0] = FTP_CUST_RST_N;
    Buffer[1] = 0x00;
    if (I2C_Write_USB_PD(Port, FTP_CTRL_0, Buffer, 2) != HAL_OK) return -1;
    
    // Clear password
    Buffer[0] = 0x00;
    if (I2C_Write_USB_PD(Port, FTP_CUST_PASSWORD_REG, Buffer, 1) != HAL_OK) return -1;
    
    return 0;
}
