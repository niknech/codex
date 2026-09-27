#include "ram_driver.h"
#include "fatfs.h"
#include "ff.h"


uint8_t ramdrive_init(void)
{
	memset(ramdrive, 0, sizeof(ramdrive));

	BYTE work[512];

	FRESULT fr = f_mkfs(USERPath, FM_FAT|FM_SFD, 0, work, sizeof(work));

	if (fr != FR_OK)
		return 0;
	else
		return 1;
}

