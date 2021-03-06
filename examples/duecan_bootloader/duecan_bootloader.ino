#include <Arduino.h>
#include "due_can.h"
#include "DueFlashStorage.h"
#include "aes.hpp"

#define CANBASE		0x79b
#define CANANSWER	0x7bb
#define DEVICETOK  0xCAFEFACE

uint32_t flashWritePosition;
DueFlashStorage dueFlashStorage;
uint8_t pageBuffer[IFLASH1_PAGE_SIZE];

#ifdef __cplusplus
extern "C" {
#endif

enum {
	START_FLASH = 0xEF,
	DATA_PACKET = 0xFF,
	END_FLASH = 0xDE
} BOOT_COMMANDS;

uint8_t aes_key[] = { 240, 32, 169, 211, 43, 17, 238, 36, 95, 60, 93, 79, 8, 58, 40, 247 };
struct AES_ctx aes_ctx;

__attribute__ ((long_call, section (".ramfunc"))) void setupForReboot()
{
	__disable_irq();

//Adapted from code found in Reset.cpp in Arduino core files for Due
//GPNVM bits are as follows:
//0 = lock bit (1 = flash memory is security locked)
//1 = Booting mode (0 = boot from ROM, 1 = boot from FLASH)
//2 = Flash ordering (0 = normal ordering FLASH0 then FLASH1, 1 = Reverse so FLASH1 is mapped first)
		
	const int EEFC_FCMD_CGPB = 0x0C;
	const int EEFC_FCMD_SGPB = 0x0B;
	const int EEFC_KEY = 0x5A;
	while ((EFC0->EEFC_FSR & EEFC_FSR_FRDY) == 0);
	// Set bootflag to run from FLASH instead of ROM
	EFC0->EEFC_FCR =
		EEFC_FCR_FCMD(EEFC_FCMD_SGPB) |
		EEFC_FCR_FARG(1) |
		EEFC_FCR_FKEY(EEFC_KEY);
	while ((EFC0->EEFC_FSR & EEFC_FSR_FRDY) == 0);	
	// Set bootflag to run from FLASH0
	EFC0->EEFC_FCR =
		EEFC_FCR_FCMD(EEFC_FCMD_CGPB) |
		EEFC_FCR_FARG(2) |
		EEFC_FCR_FKEY(EEFC_KEY);
	while ((EFC0->EEFC_FSR & EEFC_FSR_FRDY) == 0);	

	// Force a hard reset
	const int RSTC_KEY = 0xA5;
	RSTC->RSTC_CR =
		RSTC_CR_KEY(RSTC_KEY) |
		RSTC_CR_PROCRST |
		RSTC_CR_PERRST;

	while (true); //bye cruel world!
}

static void decrypt_pagebuffer()
{
	const size_t BLOCK_SIZE = 16;
	/* NOTE: IFLASH_PAGE_SIZE is divisible by BLOCK_SIZE */
	uint8_t *ptr = pageBuffer;
	for (int i = 0; i < IFLASH_PAGE_SIZE/BLOCK_SIZE; i++)
	{
		AES_ECB_decrypt(&aes_ctx, ptr);
		ptr += BLOCK_SIZE;
	}
}

void setup()
{
	flashWritePosition = 0;
	Serial.begin(115200);
	Can1.begin(500000);
	Can1.setRXFilter(CANBASE, 0x700, false);
	AES_init_ctx(&aes_ctx, aes_key);
  Serial.println("Started CAN Bootloader.");
}

void loop()
{
	CAN_FRAME inFrame;
	CAN_FRAME outFrame;
	int location, bufferWritePtr;
	if (Can1.available() > 0) {
		Can1.read(inFrame);

		switch (inFrame.data.byte[0])
		{
			case START_FLASH: //just in case we're already in the bootloader but someone sends a "go to bootloader" message to get started
				if (inFrame.data.low == (uint32_t)0xDEADBEEF)
				{
					Serial.print("@");
					if (inFrame.data.high == DEVICETOK)
					{
						Serial.println("Starting firmware upload process");
						outFrame.id = CANANSWER;
						outFrame.extended = false;
						outFrame.length = 8;
						outFrame.data.low = (uint32_t)0xDEAFDEAD;
						outFrame.data.high = DEVICETOK;
						Can1.sendFrame(outFrame);
						/* Resetting flashWritePosition */
						flashWritePosition = 0;
					}
				}
				break;
        
			case DATA_PACKET:
				Serial.print("-");
				location = inFrame.data.byte[1] + (256 * inFrame.data.byte[2]);
				bufferWritePtr = (location * 4) % IFLASH_PAGE_SIZE;
				pageBuffer[bufferWritePtr++] = inFrame.data.byte[3];
				pageBuffer[bufferWritePtr++] = inFrame.data.byte[4];
				pageBuffer[bufferWritePtr++] = inFrame.data.byte[5];
				pageBuffer[bufferWritePtr++] = inFrame.data.byte[6];
				if (bufferWritePtr == (IFLASH1_PAGE_SIZE))
				{					
					Serial.print("Writing flash at ");
					Serial.println(flashWritePosition);
					decrypt_pagebuffer();
					dueFlashStorage.write(flashWritePosition, pageBuffer, IFLASH1_PAGE_SIZE, 0);
					flashWritePosition += IFLASH1_PAGE_SIZE;
				}		
				outFrame.id = CANANSWER;
				outFrame.extended = false;
				outFrame.length = 2;
				outFrame.data.byte[0] = inFrame.data.byte[1];
				outFrame.data.byte[1] = inFrame.data.byte[2];
				Can1.sendFrame(outFrame);
				break;

			case END_FLASH:
				Serial.print("#");
				if (inFrame.data.low == 0xC0DEFADE)
				{				
					//write out the any last little bits that were received but didn't add up to a full page
					Serial.println(bufferWritePtr);
					Serial.print("Writing flash at ");
					Serial.println(flashWritePosition);
					decrypt_pagebuffer();
					dueFlashStorage.write(flashWritePosition, pageBuffer, IFLASH1_PAGE_SIZE, 0);
					Serial.println("About to set boot mode and reboot");
					delay(50);
					//switch boot section								
					setupForReboot(); //this will reboot automatically to FLASH0
				}
				break;
		}
	}
}

#ifdef __cplusplus
}
#endif
		

	



		





		

